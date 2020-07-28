## IMPORTANT NOTE
Before running any of the commands below, make sure you cd into the src folder.
```
 $ cd src
```

## Generate Protobuf Messages and Client Service Stub

To generate the protobuf messages and client service stub class from your
`.proto` definitions, we need the `protoc` binary and the
`protoc-gen-grpc-web` plugin.

You can download the `protoc-gen-grpc-web` protoc plugin from our
[release](https://github.com/grpc/grpc-web/releases) page:

If you don't already have `protoc` installed, you will have to download it
first from [here](https://github.com/protocolbuffers/protobuf/releases).

Make sure they are both executable and are discoverable from your PATH.

  1.Next, open a new tab and build the C++ gRPC Service using CMAKE (Go here for prerequisites https://grpc.io/docs/languages/cpp/quickstart/).

 ```sh
 $ mkdir -p cmake/build
 $ pushd cmake/build
 $ cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR ../..
 $ make -j
 $ ./voxualize_server
 ```