/*
 * cl_newtonemapping_handler.cpp - CL tonemapping handler
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
#include "cl_newtonemapping_handler.h"

namespace XCam {

CLNewTonemappingImageKernel::CLNewTonemappingImageKernel (SmartPtr<CLContext> &context,
        const char *name)
    : CLImageKernel (context, name)
    , _image_width (960)
    , _image_height (540)
{
    for(int i = 0; i < 65536; i++)
    {
        _map_hist[i] = i;
    }
}

void Haleq(int *y, int *hist, int *hist_leq, int left, int right, int level, int index_left, int index_right)
{
    int l;
    float e, le;

    l = (left + right) / 2;
    int num_left = left > 0 ? hist[left - 1] : 0;
    int pixel_num = hist[right] - num_left;
    e = y[num_left + pixel_num / 2];

    if(e != 0)
    {
        le = 0.5f * (e - l) + l;
    }
    else
    {
        le = l;
    }

    int index = (index_left + index_right) / 2;
    hist_leq[index] = (int)(le + 0.5f);

    if(level > 5) return;

    Haleq(y, hist, hist_leq, left, (int)(le + 0.5f), level + 1, index_left, index);
    Haleq(y, hist, hist_leq, (int)(le + 0.5f) + 1, right, level + 1, index + 1, index_right);
}

void Block_split_haleq(int* hist, int hist_bin_count, int pixel_num, int block_start_index, float* map_hist)
{
    int y_max = 0;
    float y_avg = 0.0f;

    for(int i = hist_bin_count - 1; i >= 0; i--)
    {
        if(hist[i] > 0)
        {
            y_max = i;
            break;
        }
    }

    for(int i = 0; i < hist_bin_count; i++)
    {
        y_avg += i * hist[i];
    }

    y_max = y_max + 1;
    y_avg = y_avg / pixel_num;

    int* hist_log = new int[hist_bin_count];
    int* sort_y = new int[pixel_num + 1];
    float* map_index_leq = new float[hist_bin_count];
    int* map_index_log = new int[hist_bin_count];

    for(int i = 0; i < hist_bin_count; i++)
    {
        hist_log[i] = 0;
    }

    int thres = (int)(1500 * 1500 / (y_avg * y_avg + 1) * 600);
    int y_max0 = (y_max > thres) ? thres : y_max;
    int y_max1 = (y_max - thres) > 0 ? (y_max - thres) : 0;

    float t0 = 0.01f * y_max0 + 0.001f;
    float t1 = 0.001f * y_max1 + 0.001f;
    float max0_log = log(y_max0 + t0);
    float max1_log = log(y_max1 + t1);
    float t0_log = log(t0);
    float t1_log = log(t1);
    float factor0;

    if(y_max < thres)
    {
        factor0 = (hist_bin_count - 1) / (max0_log - t0_log + 0.001f);
    }
    else
        factor0 = y_max0 / (max0_log - t0_log + 0.001f);

    float factor1 = y_max1 / (max1_log - t1_log + 0.001f);

    if(y_max < thres)
    {
        for(int i = 0; i < y_max; i++)
        {
            int index = (int)((log(i + t0) - t0_log) * factor0 + 0.5f);
            hist_log[index] += hist[i];
            map_index_log[i] = index;
        }
    }
    else
    {
        for(int i = 0; i < y_max0; i++)
        {
            int index = (int)((log(i + t0) - t0_log) * factor0 + 0.5f);
            hist_log[index] += hist[i];
            map_index_log[i] = index;
        }

        for(int i = y_max0; i < y_max; i++)
        {
            int r = y_max - i;
            int index = (int)((log(r + t1) - t1_log) * factor1 + 0.5f);
            index = y_max - index;
            hist_log[index] += hist[i];
            map_index_log[i] = index;
        }
    }

    for(int i = y_max; i < hist_bin_count; i++)
    {
        hist_log[map_index_log[y_max - 1]] += hist[i];
        map_index_log[i] = map_index_log[y_max - 1];
    }

    int sort_index = 1;
    for(int i = 0; i < hist_bin_count; i++)
    {
        for(int l = 0; l < hist_log[i]; l++)
        {
            sort_y[sort_index] = i;
            sort_index++;
        }
    }
    sort_y[0] = 0;

    for(int i = 1; i < hist_bin_count; i++)
    {
        hist_log[i] += hist_log[i - 1];
    }

    int map_leq_index[256];

    Haleq(sort_y, hist_log, map_leq_index, 0, hist_bin_count - 1, 0, 0, 255);

    map_leq_index[255] = hist_bin_count;
    map_leq_index[0] = 0;

    for(int i = 1; i < 255; i++)
    {
        if(i % 2 == 0) map_leq_index[i] = (map_leq_index[i - 1] + map_leq_index[i + 1]) / 2;
        if(map_leq_index[i] < map_leq_index[i - 1])
            map_leq_index[i] = map_leq_index[i - 1];
    }

    for(int i = 0; i < 255; i++)
    {
        for(int k = map_leq_index[i]; k < map_leq_index[i + 1]; k++)
        {
            map_index_leq[k] = (float)i;
        }
    }

    for(int i = 0; i < hist_bin_count; i++)
    {
        map_hist[i + block_start_index] = map_index_leq[map_index_log[i]] / 255.0f;
    }

    delete[] hist_log;
    delete[] map_index_leq;
    delete[] map_index_log;
    delete[] sort_y;
}

XCamReturn
CLNewTonemappingImageKernel::prepare_arguments (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output,
    CLArgument args[], uint32_t &arg_count,
    CLWorkSize &work_size)
{
    SmartPtr<CLContext> context = get_context ();

    const VideoBufferInfo & in_video_info = input->get_video_info ();

    _image_in = new CLVaImage (context, input);
    _image_out = new CLVaImage (context, output);
    _image_width = in_video_info.aligned_width;
    _image_height = in_video_info.aligned_height;

    XCAM_ASSERT (_image_in->is_valid () && _image_out->is_valid ());
    XCAM_FAIL_RETURN (
        WARNING,
        _image_in->is_valid () && _image_out->is_valid (),
        XCAM_RETURN_ERROR_MEM,
        "cl image kernel(%s) in/out memory not available", get_kernel_name ());

    SmartPtr<X3aStats> stats = input->find_3a_stats ();
    XCam3AStats *stats_ptr = stats->get_stats ();

    int block_factor = 4;
    int width_per_block = stats_ptr->info.width / block_factor;
    int height_per_block = stats_ptr->info.height / block_factor;
    int height_last_block = height_per_block + stats_ptr->info.height % block_factor;
    int hist_bin_count = 1 << stats_ptr->info.bit_depth;

    int* hist_per_block = new int[hist_bin_count];

    for(int block_row = 0; block_row < block_factor; block_row++)
    {
        for(int block_col = 0; block_col < block_factor; block_col++)
        {
            int block_start_index = (block_row * block_factor + block_col) * hist_bin_count;
            int start_index = block_row * height_per_block * stats_ptr->info.width + block_col * width_per_block;

            for(int i = 0; i < hist_bin_count; i++)
            {
                hist_per_block[i] = 0;
            }

            if(block_row == block_factor - 1)
            {
                height_per_block = height_last_block;
            }

            int block_totalnum = width_per_block * height_per_block;
            for(int i = 0; i < height_per_block; i++)
            {
                for(int j = 0; j < width_per_block; j++)
                {
                    int y = stats_ptr->stats[start_index + i * stats_ptr->info.width + j].avg_y;
                    hist_per_block[y]++;
                }
            }

            Block_split_haleq(hist_per_block, hist_bin_count, block_totalnum, block_start_index, _map_hist);
        }
    }

    delete[] hist_per_block;

    _map_hist_buffer = new CLBuffer(
        context, sizeof(float) * hist_bin_count * block_factor * block_factor,
        CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, &_map_hist);

    //set args;
    args[0].arg_adress = &_image_in->get_mem_id ();
    args[0].arg_size = sizeof (cl_mem);
    args[1].arg_adress = &_image_out->get_mem_id ();
    args[1].arg_size = sizeof (cl_mem);
    args[2].arg_adress = &_map_hist_buffer->get_mem_id ();
    args[2].arg_size = sizeof (cl_mem);
    args[3].arg_adress = &_image_width;
    args[3].arg_size = sizeof (int);
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

CLNewTonemappingImageHandler::CLNewTonemappingImageHandler (const char *name)
    : CLImageHandler (name)
    , _output_format (XCAM_PIX_FMT_SGRBG16_planar)
{
}

bool
CLNewTonemappingImageHandler::set_tonemapping_kernel(SmartPtr<CLNewTonemappingImageKernel> &kernel)
{
    SmartPtr<CLImageKernel> image_kernel = kernel;
    add_kernel (image_kernel);
    _tonemapping_kernel = kernel;
    return true;
}

XCamReturn
CLNewTonemappingImageHandler::prepare_buffer_pool_video_info (
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
create_cl_newtonemapping_image_handler (SmartPtr<CLContext> &context)
{
    SmartPtr<CLNewTonemappingImageHandler> tonemapping_handler;
    SmartPtr<CLNewTonemappingImageKernel> tonemapping_kernel;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    tonemapping_kernel = new CLNewTonemappingImageKernel (context, "kernel_newtonemapping");
    {
        XCAM_CL_KERNEL_FUNC_SOURCE_BEGIN(kernel_newtonemapping)
#include "kernel_newtonemapping.clx"
        XCAM_CL_KERNEL_FUNC_END;
        ret = tonemapping_kernel->load_from_source (kernel_newtonemapping_body, strlen (kernel_newtonemapping_body));
        XCAM_FAIL_RETURN (
            WARNING,
            ret == XCAM_RETURN_NO_ERROR,
            NULL,
            "CL image handler(%s) load source failed", tonemapping_kernel->get_kernel_name());
    }
    XCAM_ASSERT (tonemapping_kernel->is_valid ());
    tonemapping_handler = new CLNewTonemappingImageHandler("cl_handler_newtonemapping");
    tonemapping_handler->set_tonemapping_kernel(tonemapping_kernel);

    return tonemapping_handler;
}

};
