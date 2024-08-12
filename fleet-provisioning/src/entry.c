#include "fleet-provision.h"
#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <openssl/pem.h>
#include <string.h>
#include <unistd.h>

GglError run_fleet_prov(void) {
    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;
    BIO *b64= NULL;
    BUF_MEM *bptr = NULL;

    generate_key_files(pkey, csr_req);

    FILE *fp;
    

    static char csr_buf[2048]={0};
    BIO_read(b64, (void *) csr_buf, (int) cert_length);

    GGL_LOGI(
        "fleet-provisioning",
        "New String: %.*s.",
        (int) strlen(csr_buf),
        csr_buf
    );

    sleep(30);
    make_request(csr_buf);
    

    EVP_PKEY_free(pkey);
    X509_REQ_free(csr_req);

    return 0;
}
