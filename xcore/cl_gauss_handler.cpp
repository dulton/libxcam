/*
 * cl_gauss_handler.cpp - CL gauss handler
 *
 *  Copyright (c) 2016 Intel Corporation
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
 * Author: wangfei <feix.w.wang@intel.com>
 *             Wind Yuan <feng.yuan@intel.com>
 */
#include "xcam_utils.h"
#include "cl_gauss_handler.h"
#include <algorithm>

#define XCAM_GAUSS_SCALE(radius) ((radius) * 2 + 1)

namespace XCam {

CLGaussImageKernel::CLGaussImageKernel (SmartPtr<CLContext> &context, uint32_t radius, float sigma)
    : CLImageKernel (context, "kernel_gauss")
    , _g_radius (radius)
    , _g_table (NULL)
{
    set_gaussian(radius, sigma);
}

CLGaussImageKernel::~CLGaussImageKernel ()
{
    xcam_free (_g_table);
}

bool
CLGaussImageKernel::set_gaussian (uint32_t radius, float sigma)
{
    uint32_t i, j;
    uint32_t scale = XCAM_GAUSS_SCALE (radius);
    float dis = 0.0f, sum = 0.0f;

    xcam_free (_g_table);
    _g_table_buffer.release ();
    _g_radius = radius;
    _g_table = (float*) xcam_malloc0 (scale * scale * sizeof (_g_table[0]));

    for(i = 0; i < scale; i++)  {
        for(j = 0; j < scale; j++) {
            dis = ((float)i - radius) * ((float)i - radius) + ((float)j - radius) * ((float)j - radius);
            _g_table[i * scale + j] = exp(-dis / (2.0f * sigma * sigma));
            sum += _g_table[i * scale + j];
        }
    }

    for(i = 0; i < scale * scale; i++)
        _g_table[i] = _g_table[i] / sum;

    return true;
}

SmartPtr<DrmBoBuffer>
CLGaussImageKernel::get_input_parameter (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output)
{
    XCAM_UNUSED (output);
    return input;
}
SmartPtr<DrmBoBuffer>
CLGaussImageKernel::get_output_parameter (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output)
{
    XCAM_UNUSED (input);
    return output;
}

XCamReturn
CLGaussImageKernel::prepare_arguments (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output,
    CLArgument args[], uint32_t &arg_count,
    CLWorkSize &work_size)
{
    SmartPtr<CLContext> context = get_context ();
    SmartPtr<DrmBoBuffer>  input_buf = get_input_parameter (input, output);
    SmartPtr<DrmBoBuffer>  output_buf = get_output_parameter (input, output);

    XCAM_FAIL_RETURN (
        WARNING,
        input_buf.ptr () && output_buf.ptr (),
        XCAM_RETURN_ERROR_MEM,
        "cl image kernel(%s) get input/output buffer failed", get_kernel_name ());

    const VideoBufferInfo & video_info_in = input_buf->get_video_info ();
    const VideoBufferInfo & video_info_out = output_buf->get_video_info ();
    CLImageDesc cl_desc_in, cl_desc_out;

    cl_desc_in.format.image_channel_data_type = CL_UNORM_INT8;
    cl_desc_in.format.image_channel_order = CL_R;
    cl_desc_in.width = video_info_in.width;
    cl_desc_in.height = video_info_in.height;
    cl_desc_in.row_pitch = video_info_in.strides[0];
    _image_in = new CLVaImage (context, input_buf, cl_desc_in, video_info_in.offsets[0]);

    cl_desc_out.format.image_channel_data_type = CL_UNORM_INT8;
    cl_desc_out.format.image_channel_order = CL_RGBA;
    cl_desc_out.width = video_info_out.width / 4;
    cl_desc_out.height = video_info_out.height;
    cl_desc_out.row_pitch = video_info_out.strides[0];
    _image_out = new CLVaImage (context, output_buf, cl_desc_out, video_info_out.offsets[0]);

    if (!_g_table_buffer.ptr ()) {
        _g_table_buffer = new CLBuffer(
            context, sizeof(float) * XCAM_GAUSS_SCALE (_g_radius) * XCAM_GAUSS_SCALE (_g_radius),
            CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR , _g_table);
    }

    XCAM_ASSERT (_image_in->is_valid () && _image_out->is_valid ());
    XCAM_FAIL_RETURN (
        WARNING,
        _image_in->is_valid () && _image_out->is_valid (),
        XCAM_RETURN_ERROR_MEM,
        "cl image kernel(%s) in/out memory not available", get_kernel_name ());

    //set args;
    args[0].arg_adress = &_image_in->get_mem_id ();
    args[0].arg_size = sizeof (cl_mem);
    args[1].arg_adress = &_image_out->get_mem_id ();
    args[1].arg_size = sizeof (cl_mem);
    args[2].arg_adress = &_g_table_buffer->get_mem_id();
    args[2].arg_size = sizeof (cl_mem);
    arg_count = 3;

    work_size.dim = XCAM_DEFAULT_IMAGE_DIM;
    work_size.global[0] = XCAM_ALIGN_UP(cl_desc_out.width, 8);
    work_size.global[1] = XCAM_ALIGN_UP (cl_desc_out.height / 2, 4);
    work_size.local[0] = 8;
    work_size.local[1] = 4;

    return XCAM_RETURN_NO_ERROR;
}

CLGaussImageHandler::CLGaussImageHandler (const char *name)
    : CLImageHandler (name)
{
}

bool
CLGaussImageHandler::set_gaussian_table (int size, float sigma)
{
    _gauss_kernel->set_gaussian (size, sigma);
    return true;
}

bool
CLGaussImageHandler::set_gauss_kernel(SmartPtr<CLGaussImageKernel> &kernel)
{
    SmartPtr<CLImageKernel> image_kernel = kernel;
    add_kernel (image_kernel);
    _gauss_kernel = kernel;
    return true;
}

SmartPtr<CLImageHandler>
create_cl_gauss_image_handler (SmartPtr<CLContext> &context, uint32_t radius, float sigma)
{
    SmartPtr<CLGaussImageHandler> gauss_handler;
    SmartPtr<CLGaussImageKernel> gauss_kernel;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    gauss_kernel = new CLGaussImageKernel (context, radius, sigma);
    {
        char build_options[1024];
        xcam_mem_clear (build_options);
        snprintf (build_options, sizeof (build_options), " -DGAUSS_RADIUS=%d ", radius);

        XCAM_CL_KERNEL_FUNC_SOURCE_BEGIN(kernel_gauss)
#include "kernel_gauss.clx"
        XCAM_CL_KERNEL_FUNC_END;
        ret = gauss_kernel->load_from_source (
                  kernel_gauss_body, strlen (kernel_gauss_body),
                  NULL, NULL, build_options);
        XCAM_FAIL_RETURN (
            WARNING,
            ret == XCAM_RETURN_NO_ERROR,
            NULL,
            "CL image handler(%s) load source failed", gauss_kernel->get_kernel_name());
    }
    XCAM_ASSERT (gauss_kernel->is_valid ());
    gauss_handler = new CLGaussImageHandler ("cl_handler_gauss");
    gauss_handler->set_gauss_kernel (gauss_kernel);

    return gauss_handler;
}

}
