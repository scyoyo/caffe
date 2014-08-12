// Copyright Ross Girshick and Yangqing Jia 2013
//
// matcaffe.cpp provides a wrapper of the caffe::Net class as well as some
// caffe::Caffe functions so that one could easily call it from matlab.
// Note that for matlab, we will simply use float as the data type.

#include <string>
#include <vector>

#include "mex.h"
#include "caffe/caffe.hpp"

#define MEX_ARGS int nlhs, mxArray **plhs, int nrhs, const mxArray **prhs

using namespace caffe;  // NOLINT(build/namespaces)

// The pointer to the internal caffe::Net instance
static shared_ptr<Net<float> > net_;
static int init_key = -2;

// Five things to be aware of:
//   caffe uses row-major order
//   matlab uses column-major order
//   caffe uses BGR color channel order
//   matlab uses RGB color channel order
//   images need to have the data mean subtracted
//
// Data coming in from matlab needs to be in the order
//   [width, height, channels, images]
// where width is the fastest dimension.
// Here is the rough matlab for putting image data into the correct
// format:
//   % convert from uint8 to single
//   im = single(im);
//   % reshape to a fixed size (e.g., 227x227)
//   im = imresize(im, [IMAGE_DIM IMAGE_DIM], 'bilinear');
//   % permute from RGB to BGR and subtract the data mean (already in BGR)
//   im = im(:,:,[3 2 1]) - data_mean;
//   % flip width and height to make width the fastest dimension
//   im = permute(im, [2 1 3]);
//
// If you have multiple images, cat them with cat(4, ...)
//
// The actual forward function. It takes in a cell array of 4-D arrays as
// input and outputs a cell array.
// static mxArray* do_forward(const mxArray* const bottom) {
//   vector<Blob<float>*>& input_blobs = net_->input_blobs();
// //   printf("input blob count %d\n",input_blobs[0]->count());
//   CHECK_EQ(static_cast<unsigned int>(mxGetDimensions(bottom)[0]),
//       input_blobs.size());
//   for (unsigned int i = 0; i < input_blobs.size(); ++i) {
//     const mxArray* const elem = mxGetCell(bottom, i);
//     const float* const data_ptr =
//         reinterpret_cast<const float* const>(mxGetPr(elem));
//     switch (Caffe::mode()) {
//     case Caffe::CPU:
//       memcpy(input_blobs[i]->mutable_cpu_data(), data_ptr,
//           sizeof(float) * input_blobs[i]->count());
//       break;
//     case Caffe::GPU:
//       cudaMemcpy(input_blobs[i]->mutable_gpu_data(), data_ptr,
//           sizeof(float) * input_blobs[i]->count(), cudaMemcpyHostToDevice);
//       break;
//     default:
//       LOG(FATAL) << "Unknown Caffe mode.";
//     }  // switch (Caffe::mode())
//   }
//   const vector<Blob<float>*>& output_blobs = net_->ForwardPrefilled();
//   mxArray* mx_out = mxCreateCellMatrix(output_blobs.size(), 1);
//   for (unsigned int i = 0; i < output_blobs.size(); ++i) {
//     mxArray* mx_blob = mxCreateNumericMatrix(output_blobs[i]->count(),
//         1, mxSINGLE_CLASS, mxREAL);
//     mxSetCell(mx_out, i, mx_blob);
//     float* data_ptr = reinterpret_cast<float*>(mxGetPr(mx_blob));
//     switch (Caffe::mode()) {
//     case Caffe::CPU:
//       memcpy(data_ptr, output_blobs[i]->cpu_data(),
//           sizeof(float) * output_blobs[i]->count());
//       break;
//     case Caffe::GPU:
//       cudaMemcpy(data_ptr, output_blobs[i]->gpu_data(),
//           sizeof(float) * output_blobs[i]->count(), cudaMemcpyDeviceToHost);
//       break;
//     default:
//       LOG(FATAL) << "Unknown Caffe mode.";
//     }  // switch (Caffe::mode())
//   }
//
//   return mx_out;
// }

static mxArray* do_forward(const mxArray* const bottom) {
    vector<Blob<float>*>& input_blobs = net_->input_blobs();
//   printf("input blob count %d\n",input_blobs[0]->count());
    
    CHECK_EQ(static_cast<unsigned int>(mxGetDimensions(bottom)[0]),
            input_blobs.size());
    for (unsigned int i = 0; i < input_blobs.size(); ++i) {
        printf("input blob %d info:\n width: %d, height: %d, nch: %d, num: %d\n",
                i,input_blobs[i]->width(), input_blobs[i]->height(),
                input_blobs[i]->channels(), input_blobs[i]->num());
        const mxArray* const elem = mxGetCell(bottom, i);
        const float* const data_ptr =
                reinterpret_cast<const float* const>(mxGetPr(elem));
        switch (Caffe::mode()) {
            case Caffe::CPU:
                memcpy(input_blobs[i]->mutable_cpu_data(), data_ptr,
                        sizeof(float) * input_blobs[i]->count());
                break;
            case Caffe::GPU:
                cudaMemcpy(input_blobs[i]->mutable_gpu_data(), data_ptr,
                        sizeof(float) * input_blobs[i]->count(), cudaMemcpyHostToDevice);
                break;
            default:
                LOG(FATAL) << "Unknown Caffe mode.";
        }  // switch (Caffe::mode())
    }
    const vector<Blob<float>*>& output_blobs = net_->ForwardPrefilled();
    const vector<vector<Blob<float>*> >& res = net_->top_vecs();
    for (unsigned int i = 0; i < res.size(); ++i) {
        for (unsigned int j = 0; j < res[i].size(); ++j) {
            printf("responses(%d,%d) info:\n width: %d, height: %d, nch: %d, num: %d\n",
                    i,j,res[i][j]->width(), res[i][j]->height(),
                    res[i][j]->channels(), res[i][j]->num());
        }
    }
    
    // Step 2: prepare output array of structures
    const vector<string>& layer_names = net_->layer_names();
    
    mxArray* mx_layers;
    const mwSize dims[2] = {res.size()+1, 1};//with input_blobs
    const char* fnames[2] = {"responses", "layer_names"};
    mx_layers = mxCreateStructArray(2, dims, 2, fnames);
    
    // Step 3: copy responses into output
    {
        int mx_layer_index = 0;
        for (unsigned int ii = 0; ii <=res.size(); ++ii) {
            
            if (ii==0){
                const mwSize dims[2] = {input_blobs.size(), 1};
                mxArray* mx_layer_cells = NULL;
                mx_layer_cells = mxCreateCellArray(2, dims);
                mxSetField(mx_layers, mx_layer_index, "responses", mx_layer_cells);
                mxSetField(mx_layers, mx_layer_index, "layer_names",
                        mxCreateString("input"));
                mx_layer_index++;
                
                
                for (unsigned int j = 0; j < input_blobs.size(); ++j) {
                    // internally data is stored as (width, height, channels, num)
                    // where width is the fastest dimension
                    mwSize dims[4] = {input_blobs[j]->width(), input_blobs[j]->height(),
                    input_blobs[j]->channels(), input_blobs[j]->num()};
                    mxArray* mx_weights = mxCreateNumericArray(4, dims, mxSINGLE_CLASS,
                            mxREAL);
                    mxSetCell(mx_layer_cells, j, mx_weights);
                    float* weights_ptr = reinterpret_cast<float*>(mxGetPr(mx_weights));
                    
                    switch (Caffe::mode()) {
                        case Caffe::CPU:
                            memcpy(weights_ptr, input_blobs[j]->cpu_data(),
                                    sizeof(float) * input_blobs[j]->count());
                            break;
                        case Caffe::GPU:
                            CUDA_CHECK(cudaMemcpy(weights_ptr, input_blobs[j]->gpu_data(),
                                    sizeof(float) * input_blobs[j]->count(), cudaMemcpyDeviceToHost));
                            break;
                        default:
                            LOG(FATAL) << "Unknown caffe mode: " << Caffe::mode();
                    }
                }
                
                
                
                
            }
            else{
               unsigned int i=ii-1;
                
                const mwSize dims[2] = {res[i].size(), 1};
                mxArray* mx_layer_cells = NULL;
                mx_layer_cells = mxCreateCellArray(2, dims);
                mxSetField(mx_layers, mx_layer_index, "responses", mx_layer_cells);
                mxSetField(mx_layers, mx_layer_index, "layer_names",
                        mxCreateString(layer_names[i].c_str()));
                mx_layer_index++;
                
                for (unsigned int j = 0; j < res[i].size(); ++j) {
                    // internally data is stored as (width, height, channels, num)
                    // where width is the fastest dimension
                    mwSize dims[4] = {res[i][j]->width(), res[i][j]->height(),
                    res[i][j]->channels(), res[i][j]->num()};
                    mxArray* mx_weights = mxCreateNumericArray(4, dims, mxSINGLE_CLASS,
                            mxREAL);
                    mxSetCell(mx_layer_cells, j, mx_weights);
                    float* weights_ptr = reinterpret_cast<float*>(mxGetPr(mx_weights));
                    
                    switch (Caffe::mode()) {
                        case Caffe::CPU:
                            memcpy(weights_ptr, res[i][j]->cpu_data(),
                                    sizeof(float) * res[i][j]->count());
                            break;
                        case Caffe::GPU:
                            CUDA_CHECK(cudaMemcpy(weights_ptr, res[i][j]->gpu_data(),
                                    sizeof(float) * res[i][j]->count(), cudaMemcpyDeviceToHost));
                            break;
                        default:
                            LOG(FATAL) << "Unknown caffe mode: " << Caffe::mode();
                    }
                }
            }
        }
    }
        
    return mx_layers;
//   const vector<shared_ptr<Layer<float> > >& layers = net_->layers();
//
//   mxArray* mx_out = mxCreateCellMatrix(output_blobs.size(), 1);
//   for (unsigned int i = 0; i < output_blobs.size(); ++i) {
// //     printf("output blob %d info:\n width: %d, height: %d, nch: %d, num: %d\n",
// //             i,output_blobs[i]->width(), output_blobs[i]->height(),
// //             output_blobs[i]->channels(), output_blobs[i]->num());
//     mxArray* mx_blob = mxCreateNumericMatrix(output_blobs[i]->count(),
//         1, mxSINGLE_CLASS, mxREAL);
//     mxSetCell(mx_out, i, mx_blob);
//     float* data_ptr = reinterpret_cast<float*>(mxGetPr(mx_blob));
//     switch (Caffe::mode()) {
//     case Caffe::CPU:
//       memcpy(data_ptr, output_blobs[i]->cpu_data(),
//           sizeof(float) * output_blobs[i]->count());
//       break;
//     case Caffe::GPU:
//       cudaMemcpy(data_ptr, output_blobs[i]->gpu_data(),
//           sizeof(float) * output_blobs[i]->count(), cudaMemcpyDeviceToHost);
//       break;
//     default:
//       LOG(FATAL) << "Unknown Caffe mode.";
//     }  // switch (Caffe::mode())
//   }
        
//   return mx_out;
}


static mxArray* do_get_weights() {
    const vector<shared_ptr<Layer<float> > >& layers = net_->layers();
    const vector<string>& layer_names = net_->layer_names();
    printf("There is totally %d layers\n", layers.size());
    
    
    // Step 1: count the number of layers
    int num_layers = 0;
    {
        string prev_layer_name = "";
        for (unsigned int i = 0; i < layers.size(); ++i) {
            vector<shared_ptr<Blob<float> > >& layer_blobs = layers[i]->blobs();
            
            if (layer_blobs.size() == 0) {//layer->blob is its weights, so if its size is 0, so there is no weights.
                continue;
            }
//       printf("%s\n",layer_names[i].c_str());
            
            if (layer_names[i] != prev_layer_name) {
                prev_layer_name = layer_names[i];
                num_layers++;
            }
        }
    }
    
    // Step 2: prepare output array of structures
    mxArray* mx_layers;
    {
        const mwSize dims[2] = {num_layers, 1};
        const char* fnames[2] = {"weights", "layer_names"};
        mx_layers = mxCreateStructArray(2, dims, 2, fnames);
    }
    
    // Step 3: copy weights into output
    {
        string prev_layer_name = "";
        int mx_layer_index = 0;
        for (unsigned int i = 0; i < layers.size(); ++i) {
            vector<shared_ptr<Blob<float> > >& layer_blobs = layers[i]->blobs();
            if (layer_blobs.size() == 0) {
                continue;
            }
            
            mxArray* mx_layer_cells = NULL;
            if (layer_names[i] != prev_layer_name) {
                prev_layer_name = layer_names[i];
                const mwSize dims[2] = {layer_blobs.size(), 1};
                mx_layer_cells = mxCreateCellArray(2, dims);
                mxSetField(mx_layers, mx_layer_index, "weights", mx_layer_cells);
                mxSetField(mx_layers, mx_layer_index, "layer_names",
                        mxCreateString(layer_names[i].c_str()));
                mx_layer_index++;
            }
            
            for (unsigned int j = 0; j < layer_blobs.size(); ++j) {
                // internally data is stored as (width, height, channels, num)
                // where width is the fastest dimension
                mwSize dims[4] = {layer_blobs[j]->width(), layer_blobs[j]->height(),
                layer_blobs[j]->channels(), layer_blobs[j]->num()};
                mxArray* mx_weights = mxCreateNumericArray(4, dims, mxSINGLE_CLASS,
                        mxREAL);
                mxSetCell(mx_layer_cells, j, mx_weights);
                float* weights_ptr = reinterpret_cast<float*>(mxGetPr(mx_weights));
                
//         mexPrintf("layer: %s (%d) blob: %d  %d: (%d, %d, %d) %d\n",
//                 layer_names[i].c_str(), i, j, layer_blobs[j]->num(),
//                 layer_blobs[j]->height(), layer_blobs[j]->width(),
//                 layer_blobs[j]->channels(), layer_blobs[j]->count());
                
                switch (Caffe::mode()) {
                    case Caffe::CPU:
                        memcpy(weights_ptr, layer_blobs[j]->cpu_data(),
                                sizeof(float) * layer_blobs[j]->count());
                        break;
                    case Caffe::GPU:
                        CUDA_CHECK(cudaMemcpy(weights_ptr, layer_blobs[j]->gpu_data(),
                                sizeof(float) * layer_blobs[j]->count(), cudaMemcpyDeviceToHost));
                        break;
                    default:
                        LOG(FATAL) << "Unknown caffe mode: " << Caffe::mode();
                }
            }
        }
    }
    
    return mx_layers;
}

static void get_weights(MEX_ARGS) {
    plhs[0] = do_get_weights();
}

static void set_mode_cpu(MEX_ARGS) {
    Caffe::set_mode(Caffe::CPU);
}

static void set_mode_gpu(MEX_ARGS) {
    Caffe::set_mode(Caffe::GPU);
}

static void set_phase_train(MEX_ARGS) {
    Caffe::set_phase(Caffe::TRAIN);
}

static void set_phase_test(MEX_ARGS) {
    Caffe::set_phase(Caffe::TEST);
}

static void set_device(MEX_ARGS) {
    if (nrhs != 1) {
        LOG(ERROR) << "Only given " << nrhs << " arguments";
        mexErrMsgTxt("Wrong number of arguments");
    }
    
    int device_id = static_cast<int>(mxGetScalar(prhs[0]));
    Caffe::SetDevice(device_id);
}

static void get_init_key(MEX_ARGS) {
    plhs[0] = mxCreateDoubleScalar(init_key);
}

static void init(MEX_ARGS) {
    if (nrhs != 2) {
        LOG(ERROR) << "Only given " << nrhs << " arguments";
        mexErrMsgTxt("Wrong number of arguments");
    }
    
    char* param_file = mxArrayToString(prhs[0]);
    char* model_file = mxArrayToString(prhs[1]);
    
    net_.reset(new Net<float>(string(param_file)));
    net_->CopyTrainedLayersFrom(string(model_file));
    
    mxFree(param_file);
    mxFree(model_file);
    
    // NOLINT_NEXT_LINE(runtime/threadsafe_fn)
    init_key = rand();
    if (nlhs == 1) {
        plhs[0] = mxCreateDoubleScalar(init_key);
    }
}

static void forward(MEX_ARGS) {
    if (nrhs != 1) {
        LOG(ERROR) << "Only given " << nrhs << " arguments";
        mexErrMsgTxt("Wrong number of arguments");
    }
    
    plhs[0] = do_forward(prhs[0]);
}

static void is_initialized(MEX_ARGS) {
    if (!net_) {
        plhs[0] = mxCreateDoubleScalar(0);
    } else {
        plhs[0] = mxCreateDoubleScalar(1);
    }
}

/** -----------------------------------------------------------------
 ** Available commands.
 **/
struct handler_registry {
    string cmd;
    void (*func)(MEX_ARGS);
};

static handler_registry handlers[] = {
    // Public API functions
    { "forward",            forward         },
    { "init",               init            },
    { "is_initialized",     is_initialized  },
    { "set_mode_cpu",       set_mode_cpu    },
    { "set_mode_gpu",       set_mode_gpu    },
    { "set_phase_train",    set_phase_train },
    { "set_phase_test",     set_phase_test  },
    { "set_device",         set_device      },
    { "get_weights",        get_weights     },
    { "get_init_key",       get_init_key    },
    // The end.
    { "END",                NULL            },
};


/** -----------------------------------------------------------------
 ** matlab entry point: caffe(api_command, arg1, arg2, ...)
 **/
void mexFunction(MEX_ARGS) {
    if (nrhs == 0) {
        LOG(ERROR) << "No API command given";
        mexErrMsgTxt("An API command is requires");
        return;
    }
    
    { // Handle input command
        char *cmd = mxArrayToString(prhs[0]);
        bool dispatched = false;
        // Dispatch to cmd handler
        for (int i = 0; handlers[i].func != NULL; i++) {
            if (handlers[i].cmd.compare(cmd) == 0) {
                handlers[i].func(nlhs, plhs, nrhs-1, prhs+1);
                dispatched = true;
                break;
            }
        }
        if (!dispatched) {
            LOG(ERROR) << "Unknown command `" << cmd << "'";
            mexErrMsgTxt("API command not recognized");
        }
        mxFree(cmd);
    }
}
