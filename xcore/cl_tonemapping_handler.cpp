/*
 * cl_tonemapping_handler.cpp - CL tonemapping handler
 *
 *  Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *  Author: Wu Junkai <junkai.wu@intel.com>
 */
#include "xcam_utils.h"
#include "cl_tonemapping_handler.h"

namespace XCam {

CLTonemappingImageKernel::CLTonemappingImageKernel (SmartPtr<CLContext> &context,
        const char *name)
    : CLImageKernel (context, name)
    , _y_max (0.0)
    , _y_target (0.0)
{
    _wb_config.r_gain = 1.0;
    _wb_config.gr_gain = 1.0;
    _wb_config.gb_gain = 1.0;
    _wb_config.b_gain = 1.0;
}

bool
CLTonemappingImageKernel::set_wb (const XCam3aResultWhiteBalance &wb)
{
    _wb_config.r_gain = (float)wb.r_gain;
    _wb_config.gr_gain = (float)wb.gr_gain;
    _wb_config.gb_gain = (float)wb.gb_gain;
    _wb_config.b_gain = (float)wb.b_gain;
    return true;
}


XCamReturn
CLTonemappingImageKernel::prepare_arguments (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output,
    CLArgument args[], uint32_t &arg_count,
    CLWorkSize &work_size)
{
    SmartPtr<CLContext> context = get_context ();

    _image_in = new CLVaImage (context, input);
    _image_out = new CLVaImage (context, output);

    const VideoBufferInfo & in_video_info = input->get_video_info ();
    _image_height = in_video_info.aligned_height;

    XCAM_ASSERT (_image_in->is_valid () && _image_out->is_valid ());
    XCAM_FAIL_RETURN (
        WARNING,
        _image_in->is_valid () && _image_out->is_valid (),
        XCAM_RETURN_ERROR_MEM,
        "cl image kernel(%s) in/out memory not available", get_kernel_name ());

    SmartPtr<X3aStats> stats = input->find_3a_stats ();
    XCAM_ASSERT (stats.ptr ());
    XCam3AStats *stats_ptr = stats->get_stats ();
    XCAM_ASSERT (stats_ptr);

    int pixel_totalnum = stats_ptr->info.aligned_width * stats_ptr->info.aligned_height;
    int pixel_num = 0;
    int hist_bin_count = 1 << stats_ptr->info.bit_depth;
    int64_t cumulative_value = 0;
    int saturated_thresh = pixel_totalnum * 0.003f;
    int percent_90_thresh = pixel_totalnum * 0.1f;
    int medium_thresh = pixel_totalnum * 0.5f;
    float y_saturated = 0;
    float y_percent_90 = 0;
    float y_average = 0;
    float y_medium = 0;

    for (int i = (hist_bin_count - 1); i >= 0; i--)
    {
        pixel_num += stats_ptr->hist_y[i];
        if ((y_saturated == 0) && (pixel_num >= saturated_thresh))
        {
            y_saturated = i;
        }
        if ((y_percent_90 == 0) && (pixel_num >= percent_90_thresh))
        {
            y_percent_90 = i;
        }
        if ((y_medium == 0) && (pixel_num >= medium_thresh))
        {
            y_medium = i;
        }
        cumulative_value += i * stats_ptr->hist_y[i];
    }

    y_average = cumulative_value / pixel_totalnum;

    if (y_saturated < (hist_bin_count - 1)) {
        y_saturated = y_saturated + 1;
    }

    _y_target =  (hist_bin_count / y_saturated) * (1.5 * y_medium + 0.5 * y_average) / 2;

    if (_y_target < 4) {
        _y_target = 4;
    }
    if ((_y_target > y_saturated) || (y_saturated < 4)) {
        _y_target = y_saturated / 4;
    }

    _y_max = hist_bin_count * (2 * y_saturated + _y_target) / y_saturated - y_saturated - _y_target;

    _y_target = _y_target / pow(2, stats_ptr->info.bit_depth - 8);
    _y_max = _y_max / pow(2, stats_ptr->info.bit_depth - 8);
    //set args;
    args[0].arg_adress = &_image_in->get_mem_id ();
    args[0].arg_size = sizeof (cl_mem);
    args[1].arg_adress = &_image_out->get_mem_id ();
    args[1].arg_size = sizeof (cl_mem);
    args[2].arg_adress = &_y_max;
    args[2].arg_size = sizeof (float);
    args[3].arg_adress = &_y_target;
    args[3].arg_size = sizeof (float);
    args[4].arg_adress = &_image_height;
    args[4].arg_size = sizeof (int);

    arg_count = 5;

    const CLImageDesc out_info = _image_out->get_image_desc ();
    work_size.dim = XCAM_DEFAULT_IMAGE_DIM;
    work_size.global[0] = out_info.width;
    work_size.global[1] = out_info.height / 4;
    work_size.local[0] = 8;
    work_size.local[1] = 8;

    return XCAM_RETURN_NO_ERROR;
}

CLTonemappingImageHandler::CLTonemappingImageHandler (const char *name)
    : CLImageHandler (name)
    , _output_format (XCAM_PIX_FMT_SGRBG16_planar)
{
}

bool
CLTonemappingImageHandler::set_tonemapping_kernel(SmartPtr<CLTonemappingImageKernel> &kernel)
{
    SmartPtr<CLImageKernel> image_kernel = kernel;
    add_kernel (image_kernel);
    _tonemapping_kernel = kernel;
    return true;
}

bool
CLTonemappingImageHandler::set_wb_config (const XCam3aResultWhiteBalance &wb)
{
    return _tonemapping_kernel->set_wb (wb);
}


XCamReturn
CLTonemappingImageHandler::prepare_buffer_pool_video_info (
    const VideoBufferInfo &input,
    VideoBufferInfo &output)
{
    bool format_inited = output.init (_output_format, input.width, input.height);

    XCAM_FAIL_RETURN (
        WARNING,
        format_inited,
        XCAM_RETURN_ERROR_PARAM,
        "CL image handler(%s) ouput format(%s) unsupported",
        get_name (), xcam_fourcc_to_string (_output_format));

    return XCAM_RETURN_NO_ERROR;
}


SmartPtr<CLImageHandler>
create_cl_tonemapping_image_handler (SmartPtr<CLContext> &context)
{
    SmartPtr<CLTonemappingImageHandler> tonemapping_handler;
    SmartPtr<CLTonemappingImageKernel> tonemapping_kernel;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    tonemapping_kernel = new CLTonemappingImageKernel (context, "kernel_tonemapping");
    {
        XCAM_CL_KERNEL_FUNC_SOURCE_BEGIN(kernel_tonemapping)
#include "kernel_tonemapping.clx"
        XCAM_CL_KERNEL_FUNC_END;
        ret = tonemapping_kernel->load_from_source (kernel_tonemapping_body, strlen (kernel_tonemapping_body));
        XCAM_FAIL_RETURN (
            WARNING,
            ret == XCAM_RETURN_NO_ERROR,
            NULL,
            "CL image handler(%s) load source failed", tonemapping_kernel->get_kernel_name());
    }
    XCAM_ASSERT (tonemapping_kernel->is_valid ());
    tonemapping_handler = new CLTonemappingImageHandler("cl_handler_tonemapping");
    tonemapping_handler->set_tonemapping_kernel(tonemapping_kernel);

    return tonemapping_handler;
}

};
