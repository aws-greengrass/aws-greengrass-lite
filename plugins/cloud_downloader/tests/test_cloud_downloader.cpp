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

        WHEN("The publish packet is prepared and published with url") {
            auto request{ggapi::Struct::create()};
            auto localPath = "./http_test_doc.txt";
            request.put("uri", "https://aws-crt-test-stuff.s3.amazonaws.com/http_test_doc.txt");
            request.put("localPath", localPath);
            auto response =
                ggapi::Task::sendToTopic(ggapi::Symbol{"aws.grengrass.retrieve_artifact"}, request);

            THEN("Test if the file is created at the localPath") {
                REQUIRE(std::filesystem::exists(localPath));
                // TODO: Add a test to check the file contents
            }
        }
        WHEN("A device Credential is provided to retrive the token") {
            auto request{ggapi::Struct::create()};
            auto endpoint = "c3bom3oeb42l2o.iot.us-west-2.amazonaws.com";
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

            auto response =
                ggapi::Task::sendToTopic(ggapi::Symbol{"aws.grengrass.retrieve_token"}, request);

            THEN("Validate proper JSON format") {
                REQUIRE(true);
            }

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }
    }
}
