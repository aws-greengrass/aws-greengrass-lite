#include "fleet-provision.h"
#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <string.h>
#include <unistd.h>

static const char *csr
    = "-----BEGIN CERTIFICATE "
      "REQUEST-----"
      "\nMIICqjCCAZICAQEwZTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWEx\nFjAU"
      "BgNVBAcMDVNhbiBGcmFuY2lzY28xEjAQBgNVBAoMCU15Q29tcGFueTEVMBMG\nA1UEAwwMbX"
      "lkb21haW4uY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\nAQEAi3u+"
      "ZiG7jJgVKBIKNul8q7StqAlWv2IvlUPi3vusR5qounZCRI+"
      "tJowl6wf2\nk0ePBVAQAAOOYpI4yvi/"
      "SNfkb9Y+qPBrZaOhhaoB7ZvprKrQM+1z7yIFx8NEHj/"
      "k\nRhM0IRpDpOTMJXHL1FpWyMukfRM0+B3v8Q3G9s0n10jMq3J7UT+"
      "Gc9fIBu6phYgf\naKm5wTPbIJHQOwJPQrgpazEDRctxZjq+kZ8dNS6U6JefFZTTeiz7WXqn+"
      "00wB+Qm\nT+NYYfC8cFgrWmIYfoz++"
      "OudL7EC6CezQDiXvwrNxiVR5ppAD4Ae0o4BuJLuW5CE\n9JOUPTavMKozvPZeyMfFLHtQTwI"
      "DAQABoAAwDQYJKoZIhvcNAQELBQADggEBADnQ\nIWbKAWRmOnaTLlvBoPZQMht6t6nmyAZAC"
      "wAQJHXiquwwdgT/"
      "OAgnNhbbCHL6TCmr\nEo+uDp9K7jhezgUYG7udbN8NHx2x5o5gHVBxwxyv77tNjEaV/"
      "pjfR3838agoTf7X\nmJDGIbjIHo87Lv01TciwZ630fBDDzF0Z0d9kj7fLPkZgQR9T8/"
      "GNCioz/"
      "j1GoW12\n1bauMhiYwylySPUKNqW1mDPa5Dt1dbQuGkWH4Qu4WpuPhKBPGusE60StQqoh+"
      "0f6\nN1zC7WeZ8O2OKpRnSWqHo8hFTEajifeKD/"
      "I6kQWATLPkJK4Df0hHli1Sojzf7iQP\nZC7zpzOMnlgvwjATwfQ=\n-----END "
      "CERTIFICATE REQUEST-----\n";

GglError run_fleet_prov(void) {
    // EVP_PKEY *pkey = NULL;
    // X509_REQ *req = NULL;

    // generate_key_files(pkey, req);

    static char csr_buf[10024];

    memcpy(csr_buf, csr, strlen(csr));

    // GGL_LOGI(
    //     "fleet-provisioning",
    //     "New String: %.*s.",
    //     (int) strlen(global_csr),
    //     global_csr
    // );

    make_request(csr_buf);

    // EVP_PKEY_free(pkey);
    // X509_REQ_free(req);

    return 0;
}