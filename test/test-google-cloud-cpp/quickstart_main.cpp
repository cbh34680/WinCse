﻿// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! [all]
#include "google/cloud/storage/client.h"
#include <iostream>
#include <string>

int quickstart_main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Missing bucket name.\n";
        std::cerr << "Usage: quickstart <bucket-name>\n";
        return 1;
    }
    std::string const bucket_name = argv[1];

    // Create a client to communicate with Google Cloud Storage. This client
    // uses the default configuration for authentication and project id.
    auto client = google::cloud::storage::Client();

    auto writer = client.WriteObject(bucket_name, "quickstart.txt");
    writer << "Hello World!";
    writer.Close();
    if (!writer.metadata()) {
        std::cerr << "Error creating object: " << writer.metadata().status()
            << "\n";
        return 1;
    }
    std::cout << "Successfully created object: " << *writer.metadata() << "\n";

    auto reader = client.ReadObject(bucket_name, "quickstart.txt");
    if (!reader) {
        std::cerr << "Error reading object: " << reader.status() << "\n";
        return 1;
    }

    std::string contents{std::istreambuf_iterator<char>{reader}, {}};
    std::cout << contents << "\n";

    return 0;
}
//! [all]