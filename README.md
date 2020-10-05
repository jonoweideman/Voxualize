## HOW TO RUN

The following software is required in order to run Voxualize:  
  **FFmpeg (with libx264)** - downloadable from https://ffmpeg.org/download.html  
  &nbsp; &nbsp; &nbsp;&nbsp; We recommend these instructions: https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu  
  **Zfp** - installation instructions: https://zfp.readthedocs.io/en/release0.5.4/installation.html  
 **VTK (with EGL activated)** - installation instructions: https://vtk.org/Wiki/VTK/Configure_and_Build  
  `protoc` - https://github.com/protocolbuffers/protobuf/releases  
  `protoc-gen-grpc-web` - https://github.com/grpc/grpc-web/releases  
  **gRPC** - (Go here for prerequisites https://grpc.io/docs/languages/cpp/quickstart/)

Make sure they are all executable and are discoverable from your **PATH**

Before running any of the commands below, make sure the following commands are 
executed from the src folder.
```
 $ cd src
```
  1.Next, open a new tab and build the C++ gRPC Service using CMAKE (Go here for prerequisites https://grpc.io/docs/languages/cpp/quickstart/).

 ```sh
 $ mkdir -p cmake/build
 $ pushd cmake/build
 $ cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR -Dffmpeg_install_dir=/path/to/ffmpeg/install/dir -Dffmpeg_lib_dir=/path/to/ffmpeg/libs -Dzfp_lib_dir=/path/to/zfp/install/dir ../..
 $ make -j
 $ ./voxualize_server
 ```
**Required variables:**
 `ffmpeg_install_dir` - the root of the ffmpeg install.
 `ffmpeg_lib_dir` - the direcotry containing libavcodec.so, libavutil.so and libswscale.so.
 `zfp_lib_dir` - the directory containing libzfp.so.

 ## How to add new input data files

 Currently, Voxualize only accepts raw floats as input.
 If you wish to add a file to be visualizaed on the frontend, add the file to the Data/ directory and add an entry in File_Information.txt with the following format:

  `<name> float <dim_x> <dim_y> <dim_z>`

  For example: `milky_way.arr float 300 300 200`

This is required in order that Voxualize knows the dimensions of the data cube.

## Brief explanation on mian files of Voxualize

**src/voxualize_server.cxx** handles the main functionality of the server. It handles all the RPC request, as well as encoding and compression.

**src/data_cube.cxx** is responsible for storing the data cube, LOD model generation, cropping cubes, and keeping track of the information about the visualization on the frontend.