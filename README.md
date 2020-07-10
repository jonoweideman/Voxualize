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

When you have both `protoc` and `protoc-gen-grpc-web` installed, you can now
run this command:

```sh
$ protoc -I=./protos/ helloworld.proto   --js_out=import_style=commonjs:./protos/   --grpc-web_out=import_style=commonjs,mode=grpcwebtext:./protos/

```

After the command runs successfully, you should now see two new files generated
in the current directory:

 - `helloworld_pb.js`: this contains the `HelloRequest` and `HelloReply`
   classes
 - `helloworld_grpc_web_pb.js`: this contains the `GreeterClient` class

  1.Next, open a new tab and build the C++ gRPC Service using CMAKE (Go here for prerequisites https://grpc.io/docs/languages/cpp/quickstart/).

 ```sh
 $ mkdir -p cmake/build
 $ pushd cmake/build
 $ cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR ../..
 $ make -j
 $ ./greeter_server
 ```