/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>

#include "grpcpp/grpcpp.h"

#include "indgen_conn.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using indgen_conn::HelloRequest;
using indgen_conn::HelloReply;
using indgen_conn::Greeter;

using indgen_conn::Lemma;
using indgen_conn::Ack;

using indgen_conn::Query;
using indgen_conn::Answer;

class GrpcClient {
public:
    GrpcClient(std::shared_ptr<Channel> channel)
        : stub_(Greeter::NewStub(channel)) {}

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    std::string SayHello(const std::string& user) {
        // Data we are sending to the server.
        HelloRequest request;
        request.set_name(user);

        // Container for the data we expect from the server.
        HelloReply reply;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        // The actual RPC.
        Status status = stub_->SayHello(&context, request, &reply);

        // Act upon its status.
        if (status.ok()) {
            return reply.message();
        } else {
            std::cout << status.error_code() << ": " << status.error_message()
                      << std::endl;
            return "RPC failed";
        }
    }

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    bool SendLemma(const std::string& lemma_before,
                          const std::string& lemma_after){
        // Data we are sending to the server.
        Lemma request;
        request.set_lemma_before(lemma_before);
        request.set_lemma_after(lemma_after);
        // Container for the data we expect from the server.
        Ack ack;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        // The actual RPC.
        Status status = stub_->SendLemma(&context, request, &ack);

        // Act upon its status.
        if (status.ok()) {
            return ack.ack_message();
        } else {
            std::cout << status.error_code() << ": " << status.error_message()
                      << std::endl;
            return "RPC failed";
        }
    }
    bool QueryModel(const std::string& lemma,
                    const std::vector<int> kept_lits, const int checking_lit, const std::vector<int> to_be_checked_lits){
        // Data we are sending to the server.
        Query request;
        request.set_lemma(lemma);
        for (auto it = begin (kept_lits); it != end (kept_lits); ++it) {
            request.add_kept_lits(*it);
        }
        for (auto it = begin (to_be_checked_lits); it != end (to_be_checked_lits); ++it) {
            request.add_to_be_checked_lits(*it);
        }
        request.set_checking_lit(checking_lit);
        // Container for the data we expect from the server.
        Answer ans;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        // The actual RPC.
        Status status = stub_->QueryModel(&context, request, &ans);

        // Act upon its status.
        if (status.ok()) {
            return ans.answer()[0] > 0;
        } else {
            return true;
        }
    }
private:
    std::unique_ptr<Greeter::Stub> stub_;
};

