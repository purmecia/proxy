FROM ubuntu:latest

# Install dependencies
RUN apt-get update && \
    apt-get install -y g++ make

# Copy the source code to the container
COPY . /app

# Compile the code
WORKDIR /app
RUN make -B proxy

# Define the entrypoint command
ENTRYPOINT ["/bin/bash", "/app/entrypoint.sh"]