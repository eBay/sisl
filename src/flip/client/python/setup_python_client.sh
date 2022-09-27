pip install grpcio-tools
python3 -m grpc_tools.protoc -I proto/ --python_out=src/client/python/gen_src --grpc_python_out=src/client/python/gen_src/ proto/*.proto
