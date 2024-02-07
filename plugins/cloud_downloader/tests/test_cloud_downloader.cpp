#include "plugin.hpp"
#include "test_util.hpp"
#include <catch2/catch_all.hpp>

#include <filesystem>
#include <iostream>

SCENARIO("Example dowload from a url sent over LPC", "[cloudDownder]") {
    GIVEN("Inititate the plugin") {
        // start the lifecycle
        auto moduleScope = ggapi::ModuleScope::registerGlobalPlugin(
            "plugin", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        TestCloudDownloader sender = TestCloudDownloader(moduleScope);
        moduleScope.setActive();
        CHECK(sender.startLifecycle());

        WHEN("A device Credential is provided to retrive the token") {

            auto request{ggapi::Struct::create()};

            auto endpoint = ""; // your cred endpoint here
            auto thingName = ""; // your device thingName
            auto certPath = ""; // your CertPath
            auto caPath = ""; // your CAPath
            auto caFile = ""; // your CAFile
            auto pkeyPath = ""; // your pkeyPath

            std::stringstream ss;
            ss << "https://" << endpoint << "/role-aliases/"
               << "GreengrassV2TokenExchangeRoleAlias"
               << "/credentials";

            request.put("uri", ss.str());
            request.put("thingName", thingName);
            request.put("certPath", certPath);
            request.put("caPath", caPath);
            request.put("caFile", caFile);
            request.put("pkeyPath", pkeyPath);

            auto response = ggapi::Task::sendToTopic(
                ggapi::Symbol{"aws.greengrass.fetch_TES_from_cloud"}, request);

            THEN("Validate proper JSON format") {
                auto responseAsString = response.get<std::string>("Response");

                auto jsonHandle = ggapi::Buffer::create()
                                      .put(0, std::string_view(responseAsString.c_str()))
                                      .fromJson();

                auto jsonStruct = ggapi::Struct{jsonHandle};
                REQUIRE(jsonStruct.hasKey("credentials"));
                auto innerStruct = jsonStruct.get<ggapi::Struct>("credentials");
                REQUIRE(innerStruct.hasKey("accessKeyId"));
                REQUIRE(innerStruct.hasKey("secretAccessKey"));
                REQUIRE(innerStruct.hasKey("sessionToken"));
            }

            WHEN("The publish packet is prepared and published with url") {
                auto request{ggapi::Struct::create()};
                auto localPath = "./http_test_doc.txt";
                request.put("uri", "https://aws-crt-test-stuff.s3.amazonaws.com/http_test_doc.txt");
                request.put("localPath", localPath);
                auto response = ggapi::Task::sendToTopic(
                    ggapi::Symbol{"aws.greengrass.retrieve_artifact"}, request);

                THEN("Test if the file is created at the localPath") {
                    REQUIRE(std::filesystem::exists(localPath));
                    // TODO: Add a test to check the file contents
                }
            }

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }
    }
}
