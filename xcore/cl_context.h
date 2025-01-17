/*
 * cl_context.h - CL context
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
 * Author: Wind Yuan <feng.yuan@intel.com>
 */

#ifndef XCAM_CL_CONTEXT_H
#define XCAM_CL_CONTEXT_H

#include "xcam_utils.h"
#include "smartptr.h"
#include "cl_event.h"
#include <map>
#include <list>
#include <CL/cl.h>
#include <CL/cl_intel.h>

namespace XCam {

class CLKernel;
class CLDevice;
class CLCommandQueue;

/* correct usage
 *  SmartPtr<CLContext> context = CLDevice::instance()->get_context();
 */

class CLContext {
    //typedef std::map<std::string, SmartPtr<CLKernel>> CLKernelMap;
    typedef std::list<SmartPtr<CLCommandQueue>> CLCmdQueueList;

    friend class CLDevice;
    friend class CLKernel;
    friend class CLMemory;
    friend class CLBuffer;
    friend class CLVaBuffer;
    friend class CLVaImage;
    friend class CLImage2D;
    friend class CLImage2DArray;

public:
    enum KernelBuildType {
        KERNEL_BUILD_BINARY = 0,
        KERNEL_BUILD_SOURCE,
    };

    ~CLContext ();
    cl_context get_context_id () {
        return _context_id;
    }

    XCamReturn flush ();
    XCamReturn finish ();

    void terminate ();

private:
    static void context_pfn_notify (
        const char* erro_info, const void *private_info,
        size_t cb, void *user_data);
    static void program_pfn_notify (
        cl_program program, void *user_data);

    explicit CLContext (SmartPtr<CLDevice> &device);
    SmartPtr<CLCommandQueue> create_cmd_queue (SmartPtr<CLContext> &self);
    cl_kernel generate_kernel_id (
        CLKernel *kernel,
        const uint8_t *source,
        size_t length,
        KernelBuildType type,
        uint8_t **gen_binary,
        size_t *binary_size,
        const char *build_option);
    void destroy_kernel_id (cl_kernel &kernel_id);
    XCamReturn execute_kernel (
        CLKernel *kernel,
        CLCommandQueue *queue = NULL,
        CLEventList &events_wait = CLEvent::EmptyList,
        SmartPtr<CLEvent> &event_out = CLEvent::NullEvent);
    //bool insert_kernel (SmartPtr<CLKernel> &kernel);

    bool init_context ();
    void destroy_context ();
    bool is_valid () const {
        return (_context_id != NULL);
    }

    bool init_cmd_queue (SmartPtr<CLContext> &self);
    SmartPtr<CLCommandQueue> get_default_cmd_queue ();

    //Memory, Image
    cl_mem create_va_buffer (uint32_t bo_name);
    cl_mem import_dma_buffer (const cl_import_buffer_info_intel &import_info);
    cl_mem create_va_image (const cl_libva_image &image_info);
    cl_mem import_dma_image (const cl_import_image_info_intel &image_info);
    cl_mem create_image (
        cl_mem_flags flags, const cl_image_format& format,
        const cl_image_desc &image_info, void *host_ptr = NULL);
    void destroy_mem (cl_mem mem_id);

    // Buffer
    cl_mem create_buffer (uint32_t size, cl_mem_flags  flags, void *host_ptr);
    XCamReturn enqueue_read_buffer (
        cl_mem buf_id, void *ptr,
        uint32_t offset, uint32_t size,
        bool block = true,
        CLEventList &events_wait = CLEvent::EmptyList,
        SmartPtr<CLEvent> &event_out = CLEvent::NullEvent);

    XCamReturn enqueue_write_buffer (
        cl_mem buf_id, void *ptr,
        uint32_t offset, uint32_t size,
        bool block = true,
        CLEventList &events_wait = CLEvent::EmptyList,
        SmartPtr<CLEvent> &event_out = CLEvent::NullEvent);

    XCamReturn enqueue_map_buffer (
        cl_mem buf_id, void *&ptr,
        uint32_t offset, uint32_t size,
        bool block = true,
        cl_map_flags map_flags = CL_MEM_READ_WRITE,
        CLEventList &events_wait = CLEvent::EmptyList,
        SmartPtr<CLEvent> &event_out = CLEvent::NullEvent);

    XCamReturn enqueue_unmap (
        cl_mem mem_id,
        void *ptr,
        CLEventList &events_wait = CLEvent::EmptyList,
        SmartPtr<CLEvent> &event_out = CLEvent::NullEvent);

    int32_t export_mem_fd (cl_mem mem_id);

    // return valid event count
    static uint32_t event_list_2_id_array (
        CLEventList &events_wait,
        cl_event *cl_events, uint32_t max_count);

    XCAM_DEAD_COPY (CLContext);

private:
    cl_context                  _context_id;
    SmartPtr<CLDevice>          _device;
    //CLKernelMap                 _kernel_map;
    CLCmdQueueList              _cmd_queue_list;
};

class CLCommandQueue {
    friend class CLContext;

public:
    virtual ~CLCommandQueue ();
    cl_command_queue get_cmd_queue_id () {
        return _cmd_queue_id;
    }
    XCamReturn execute_kernel (SmartPtr<CLKernel> &kernel);

private:
    explicit CLCommandQueue (SmartPtr<CLContext> &context, cl_command_queue id);
    void destroy ();
    XCAM_DEAD_COPY (CLCommandQueue);

private:
    SmartPtr<CLContext>     _context;
    cl_command_queue        _cmd_queue_id;
};

};

#endif //XCAM_CL_CONTEXT_H
