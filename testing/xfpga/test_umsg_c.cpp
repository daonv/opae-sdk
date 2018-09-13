// Copyright(c) 2017-2018, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include "types_int.h"
#include "xfpga.h"
#include "intel-fpga.h"
#include <cstdarg>
#include <linux/ioctl.h>

#include "gtest/gtest.h"
#include "test_system.h"

#undef FPGA_MSG
#define FPGA_MSG(fmt, ...) \
	printf("MOCK " fmt "\n", ## __VA_ARGS__)

using namespace opae::testing;

int umsg_port_info(mock_object * m, int request, va_list argp){
    int retval = -1;
    errno = EINVAL;
    static bool gEnableIRQ = false;
    UNUSED_PARAM(m);
    UNUSED_PARAM(request);
    struct fpga_port_info *pinfo = va_arg(argp, struct fpga_port_info *);
    if (!pinfo) {
        FPGA_MSG("pinfo is NULL");
        goto out_EINVAL;
    }
    if (pinfo->argsz != sizeof(*pinfo)) {
        FPGA_MSG("wrong structure size");
        goto out_EINVAL;
    }
    pinfo->flags = 0;
    pinfo->num_regions = 1;
    pinfo->num_umsgs = 8;
    if (gEnableIRQ) {
        pinfo->capability = FPGA_PORT_CAP_ERR_IRQ | FPGA_PORT_CAP_UAFU_IRQ;
        pinfo->num_uafu_irqs = 1;
    } else {
        pinfo->capability = 0;
        pinfo->num_uafu_irqs = 0;
    }
    retval = 0;
    errno = 0;
out:
    return retval;

out_EINVAL:
    retval = -1;
    errno = EINVAL;
    goto out;

}

int umsg_set_mode(mock_object * m, int request, va_list argp){
    int retval = -1;
    errno = EINVAL;
    UNUSED_PARAM(m);
    UNUSED_PARAM(request);
    struct fpga_port_umsg_cfg *ucfg = va_arg(argp, struct fpga_port_umsg_cfg *);
    if (!ucfg) {
    	FPGA_MSG("ucfg is NULL");
    	goto out_EINVAL;
    }
    if (ucfg->argsz != sizeof(*ucfg)) {
    	FPGA_MSG("wrong structure size");
    	goto out_EINVAL;
    }
    if (ucfg->flags != 0) {
    	FPGA_MSG("unexpected flags %u", ucfg->flags);
    	goto out_EINVAL;
    }
    /* TODO: check hint_bitmap */
    if (ucfg->hint_bitmap >> 8) {
    	FPGA_MSG("invalid hint_bitmap 0x%x", ucfg->hint_bitmap);
    	goto out_EINVAL;
    }
    retval = 0;
    errno = 0;
out:
    return retval;

out_EINVAL:
    retval = -1;
    errno = EINVAL;
    goto out;
}

class umsg_c_p
    : public ::testing::TestWithParam<std::string> {
 protected:
  umsg_c_p() : tmpsysfs_("mocksys-XXXXXX"), handle_(nullptr) {}

  virtual void SetUp() override {
    ASSERT_TRUE(test_platform::exists(GetParam()));
    platform_ = test_platform::get(GetParam());
    system_ = test_system::instance();
    system_->initialize();
    tmpsysfs_ = system_->prepare_syfs(platform_);

    ASSERT_EQ(xfpga_fpgaGetProperties(nullptr, &filter_), FPGA_OK);
    ASSERT_EQ(xfpga_fpgaPropertiesSetObjectType(filter_, FPGA_ACCELERATOR), FPGA_OK);
    ASSERT_EQ(xfpga_fpgaEnumerate(&filter_, 1, tokens_.data(), tokens_.size(),
                            &num_matches_),
              FPGA_OK);
    ASSERT_EQ(xfpga_fpgaOpen(tokens_[0], &handle_, 0), FPGA_OK);
    system_->register_ioctl_handler(FPGA_PORT_GET_INFO, umsg_port_info);
  }

  virtual void TearDown() override {
    EXPECT_EQ(xfpga_fpgaDestroyProperties(&filter_), FPGA_OK);
    if (handle_ != nullptr) EXPECT_EQ(xfpga_fpgaClose(handle_), FPGA_OK);
    if (!tmpsysfs_.empty() && tmpsysfs_.size() > 1) {
      std::string cmd = "rm -rf " + tmpsysfs_;
      std::system(cmd.c_str());
    }
    system_->finalize();
  }

  std::string tmpsysfs_;
  fpga_properties filter_;
  std::array<fpga_token, 2> tokens_;
  fpga_handle handle_;
  uint32_t num_matches_;
  test_platform platform_;
  test_system *system_;
};

/**
 * @test       umsg_c_p
 * @brief      test_umsg_drv_01
 * @details    When the parameters are valid and the drivers are loaded,
 *             fpgaUmsgGetNumber returns number of umsgs supported by
 *             slot.
 *
 */
TEST_P (umsg_c_p, test_umsg_drv_01) {
  uint64_t Umsg_num = 0;

  EXPECT_NE(FPGA_OK, xfpga_fpgaGetNumUmsg(handle_, NULL));
  // get umsg number
  EXPECT_EQ(FPGA_OK, xfpga_fpgaGetNumUmsg(handle_, &Umsg_num));
  EXPECT_GT(Umsg_num, 0);
}

TEST_P(umsg_c_p, get_num_umsg_ioctl_err) {
  uint64_t num = 0;
  // register an ioctl handler that will return -1 and set errno to EINVAL
  system_->register_ioctl_handler(FPGA_PORT_GET_INFO, dummy_ioctl<-1,EINVAL>);
  EXPECT_EQ(FPGA_INVALID_PARAM, xfpga_fpgaGetNumUmsg(handle_, &num));

  // register an ioctl handler that will return -1 and set errno to EFAULT
  system_->register_ioctl_handler(FPGA_PORT_GET_INFO, dummy_ioctl<-1,EFAULT>);
  EXPECT_EQ(FPGA_INVALID_PARAM, xfpga_fpgaGetNumUmsg(handle_, &num));

  // register an ioctl handler that will return -1 and set errno to something
  // else
  system_->register_ioctl_handler(FPGA_PORT_GET_INFO, dummy_ioctl<-1,ENOTSUP>);
  EXPECT_EQ(FPGA_EXCEPTION, xfpga_fpgaGetNumUmsg(handle_, &num));
}

TEST_P(umsg_c_p, set_umsg_attr_ioctl_err) {
  uint64_t value = 0;
  // register an ioctl handler that will return -1 and set errno to EINVAL
  system_->register_ioctl_handler(FPGA_PORT_UMSG_SET_MODE, dummy_ioctl<-1,EFAULT>);
  EXPECT_EQ(FPGA_INVALID_PARAM, xfpga_fpgaSetUmsgAttributes(handle_, value));


  // register an ioctl handler that will return -1 and set errno to something
  // else
  system_->register_ioctl_handler(FPGA_PORT_GET_INFO, dummy_ioctl<-1,ENOTSUP>);
  EXPECT_EQ(FPGA_INVALID_PARAM, xfpga_fpgaSetUmsgAttributes(handle_, value));
}
	////////////////////////////////////////
	// Disable this test because it modifies
	// handle to gain coverage.
	////////////////////////////////////////

/**
 * @test       umsg_c_p
 * @brief      test_umsg_drv_02
 * @details    When the parameters are invalid and the drivers are loaded,
 *             fpgaUmsgGetNumber returns error.
 *
 */
//TEST_P (umsg_c_p, test_umsg_drv_02) {
//  uint64_t Umsg_num = 0;
//  int fddev = -1;
//
//  // NULL Driver hnadle
//  EXPECT_NE(FPGA_OK, xfpga_fpgaGetNumUmsg(NULL, &Umsg_num));
//
//
//  struct _fpga_handle* _handle = (struct _fpga_handle*)handle_;
//  _handle->magic = 0x123;
//
//  EXPECT_NE(FPGA_OK, xfpga_fpgaGetNumUmsg(_handle, &Umsg_num));
//
//  _handle->magic = FPGA_HANDLE_MAGIC;
//}


	////////////////////////////////////////
	// Disable this test because it modifies
	// handle to gain coverage.
	////////////////////////////////////////
/**
 * @test       umsg_c_p
 * @brief      test_umsg_drv_03
 * @details    When the parameters are invalid and the drivers are loaded,
 *             fpgaUmsgGetNumber returns error.
 *
 */
//TEST_P (umsg_c_p, test_umsg_drv_03) {
//  uint64_t Umsg_num = 0;
//  int fddev = -1;
//
//  // NULL Driver hnadle
//  EXPECT_NE(FPGA_OK, xfpga_fpgaGetNumUmsg(NULL, &Umsg_num));
//
//  // Invlaid Input Paramter
//  EXPECT_NE(FPGA_OK, xfpga_fpgaGetNumUmsg(handle_, NULL));
//
//  struct _fpga_handle* _handle = (struct _fpga_handle*)handle_;
//
//  fddev = _handle->fddev;
//  _handle->fddev = -1;
//
//  EXPECT_NE(FPGA_OK, xfpga_fpgaGetNumUmsg(handle_, &Umsg_num));
//
//  _handle->fddev = fddev;
//


/**
 * @test       Umsg_drv_04
 *
 * @brief      When the parameters are valid and the drivers are loaded,
 *             fpgaUmsgSetAttributes sets umsg hit  Enable / Disable.
 *
 */
TEST_P(umsg_c_p, test_umsg_drv_04) {
  uint64_t Umsghit_Enable = 0xffff;
  uint64_t Umsghit_Disble = 0;

  // Set umsg hint
  system_->register_ioctl_handler(FPGA_PORT_UMSG_SET_MODE,umsg_set_mode);
  EXPECT_NE(FPGA_OK, xfpga_fpgaSetUmsgAttributes(handle_, Umsghit_Enable));
  EXPECT_EQ(FPGA_OK, xfpga_fpgaSetUmsgAttributes(handle_, Umsghit_Disble));
}



/**
 * @test       Umsg_drv_04
 *
 * @brief      When the parameters are Invalid and the drivers are
 *             loaded, fpgaUmsgSetAttributes retuns error.
 *
 */
TEST_P(umsg_c_p, test_umsg_drv_05) {
  uint64_t Umsghit_Disble = 0;
//  int fddev = -1;

  system_->register_ioctl_handler(FPGA_PORT_UMSG_SET_MODE,umsg_set_mode);
  // NULL Driver hnadle
  EXPECT_NE(FPGA_OK, xfpga_fpgaSetUmsgAttributes(NULL, Umsghit_Disble));

	////////////////////////////////////////
	// Disable the following lines because it 
	// modifies handle to gain coverage.
	////////////////////////////////////////
  
  //struct _fpga_handle* _handle = (struct _fpga_handle*)handle_;
  //_handle->magic = 0x123;

  //EXPECT_NE(FPGA_OK, xfpga_fpgaSetUmsgAttributes(handle_, Umsghit_Disble));

  //_handle->magic = FPGA_HANDLE_MAGIC;
  //EXPECT_EQ(FPGA_OK, xfpga_fpgaClose(handle_));

  // // Invalid Driver handle
  // ASSERT_EQ(FPGA_OK, xfpga_fpgaOpen(tokens_[0], &handle_, 0));
  // _handle = (struct _fpga_handle*)handle_;

  // fddev = _handle->fddev;
  // _handle->fddev = -1;

  // EXPECT_NE(FPGA_OK, xfpga_fpgaSetUmsgAttributes(handle_, Umsghit_Disble));

  // _handle->fddev = fddev;
  // EXPECT_EQ(FPGA_OK, xfpga_fpgaClose(handle_));

  // // Invlaid Input Paramter
  // ASSERT_EQ(FPGA_OK, xfpga_fpgaOpen(tok, &h, 0));

  EXPECT_NE(FPGA_OK, xfpga_fpgaSetUmsgAttributes(handle_, 0xFFFFFFFF));
}

/**
 * @test       Umsg_drv_06
 *
 * @brief      When the parameters are valid and the drivers are loaded,
 *             xfpga_fpgaGetUmsgPtr returns umsg address.
 *
 */
TEST_P(umsg_c_p, test_umsg_drv_06) {
  uint64_t* umsg_ptr = NULL;
  fpga_result res;

  // Get umsg buffer
  //system_->register_ioctl_handler(FPGA_PORT_UMSG_ENABLE, dummy_ioctl<-1,EINVAL>);
  res = xfpga_fpgaGetUmsgPtr(handle_, &umsg_ptr);
  EXPECT_EQ(FPGA_OK, res);
  EXPECT_TRUE(umsg_ptr != NULL) << "\t this is umsg:" << res;
  printf("umsg_ptr %p", umsg_ptr);
}

/**
 * @test       Umsg_drv_07
 *
 * @brief      When the parameters are invalid and the drivers are
 *             loaded, xfpga_fpgaGetUmsgPtr returns uerror.
 *
 */
TEST_P(umsg_c_p, test_umsg_drv_07) {
  uint64_t* umsg_ptr = NULL;
//  int fddev = -1;

  // NULL Driver hnadle
  EXPECT_NE(FPGA_OK, xfpga_fpgaGetUmsgPtr(NULL, &umsg_ptr));

	////////////////////////////////////////
	// Disable the following lines because it 
	// modifies handle to gain coverage.
	////////////////////////////////////////

  //// Invalid Magic Number
  //ASSERT_EQ(FPGA_OK, xfpga_fpgaOpen(tokens_[0], &h, 0));

  //struct _fpga_handle* _handle = (struct _fpga_handle*)handle_;
  //_handle->magic = 0x123;

  //EXPECT_NE(FPGA_OK, xfpga_fpgaGetUmsgPtr(handle_, &umsg_ptr));

  //_handle->magic = FPGA_HANDLE_MAGIC;
  //EXPECT_EQ(FPGA_OK, xfpga_fpgaClose(handle_));

  //// Invalid Driver handle
  //ASSERT_EQ(FPGA_OK, xfpga_fpgaOpen(tokens_[0], &handle_, 0));
  //_handle = (struct _fpga_handle*)handle_;

  //fddev = _handle->fddev;
  //_handle->fddev = -1;

  //EXPECT_NE(FPGA_OK, xfpga_fpgaGetUmsgPtr(handle_, &umsg_ptr));

  //_handle->fddev = fddev;
  //EXPECT_EQ(FPGA_OK, xfpga_fpgaClose(h));

  //// Invalid Input Parameter
  //ASSERT_EQ(FPGA_OK, xfpga_fpgaOpen(tokend_[0], &handle_, 0));

  EXPECT_NE(FPGA_OK, xfpga_fpgaGetUmsgPtr(handle_, NULL));
}

/**
 * @test       Umsg_08
 *
 * @brief      When the handle parameter to xfpga_fpgaTriggerUmsg<br>
 *             is NULL, the function returns FPGA_INVALID_PARAM.<br>
 *
 */
TEST_P(umsg_c_p, test_umsg_drv_08) {
  EXPECT_EQ(FPGA_INVALID_PARAM, xfpga_fpgaTriggerUmsg(NULL, 0));
}



	////////////////////////////////////////
	// Disable this test because it modifies
	// handle to gain coverage.
	////////////////////////////////////////
/**
 * @test       Umsg_08
 *
 * @brief      When the handle parameter to xfpga_fpgaTriggerUmsg<br>
 *             has an invalid fddev,<br>
 *             Then the function returns FPGA_INVALID_PARAM.<br>
 *
 */
//TEST(umsg_c_p, test_umsg_08) {
//  struct _fpga_handle *_h = (struct _fpga_handle *) handle_;
//
//  int save_fddev = _h->fddev;
//
//  _h->fddev = -1;
//  EXPECT_EQ(FPGA_INVALID_PARAM, xfpga_fpgaTriggerUmsg(h, 0));
//
//  _h->fddev = save_fddev;
//}

INSTANTIATE_TEST_CASE_P(umsg_c, umsg_c_p, ::testing::ValuesIn(test_platform::keys(true)));