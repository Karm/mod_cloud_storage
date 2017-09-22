/*
 * @author Michal Karm Babacek <karm@fedoraproject.org>
 *
 * LICENSE, See LICENSE
 * TODO: Description
 *
 * TODO: print usage, help
 * TODO: unsigned char* vs. char* hell, apr vs. openssl "string" taking parameters
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_time.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_base64.h>
#include <apr_escape.h>
#include <apr_pools.h>
#include <apr_file_info.h>
#include <apr_md5.h>

#include <curl/curl.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

// TODO: CMake injected version?
#define USER_AGENT   "mod_cloud_storage 0.3"

//TODO: Isn't it just stupid to assume 1K? Make it configurable? LIST_BLOBS XML response listing 1 blob called "test" is 630 bytes long...
#define INITIAL_RESPONSE_MEM 1024

//Azure constants  (TODO: some namespace system...?)
#define AUTHORIZATION "SharedKey"

//TODO: Error and Debug messages...
#define DEBUG 1
#define DIE(X, ...) fprintf(stderr, "ERROR %s:%d: " X "\n", __FILE__, __LINE__, ##__VA_ARGS__); exit(EXIT_FAILURE);
#define ERR_MSG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define DEBUG_MSG(fmt, ...) do { if (DEBUG) {fprintf(stderr, fmt, ##__VA_ARGS__);}} while (0)

// Global context
CURL *curl;
const struct my_response data;
FILE *headerfile;
struct curl_slist *headers = NULL;
// Global, due to cURL callback functions, TODO: Find a better way.
apr_pool_t *pool;
apr_size_t file_length;

struct my_response {
    size_t size;
    char* response_body;
};

enum {
    UNDEFINED        = 0x0001,
    READ_BLOB        = 0x0002,
    WRITE_BLOB       = 0x0004,
    LIST_BLOBS       = 0x0008,
    CREATE_CONTAINER = 0x0010,
    TEST_REGIME      = 0x0020
};

struct cloud_configuration {
    // Credentials.
    char* azure_storage_key;
    char* azure_storage_account;
    // Container to be used.
    char* azure_container;
    // API URL to access
    char* blob_store_url;
    // Blob in that container.
    char* blob_name;
    // If specified, we R/W from/to the file, stdin/stdout is used otherwise.
    char* path_to_file;
    // A single operation to be carried out, e.g. read a blob, write a blob etc.
    unsigned int action;
};
typedef struct cloud_configuration cloud_configuration;

void free_connection() {
    curl_slist_free_all(headers);
    if(curl) {
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct my_response *mem = (struct my_response *) userp;

    mem->response_body = apr_palloc(pool, APR_ALIGN_DEFAULT(mem->size + realsize + 1));

    if(mem->response_body == NULL) {
        //TODO: We cannot do this in a module
        DIE("Failed to allocate memory.\n");
    }
    memcpy(&(mem->response_body[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->response_body[mem->size] = 0;

    return realsize;
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    /* This API is designed to read from files, streams, but we already did that, we have the data ready.
     * Supressing -Wunused-parameter with:
     */
    (void)size;
    (void)nmemb;

    memcpy(ptr, stream, file_length);
    return file_length;
}

const char* produce_authorization_header(char* azure_storage_key, char* azure_storage_account, char* string_to_sign) {
    // Decoding base64 key to binary and signing the request.
    const char* base64_decoded_key = apr_palloc(pool, APR_ALIGN_DEFAULT(apr_base64_decode_len(azure_storage_key)));
    int base64_decoded_key_len = apr_base64_decode((char*)base64_decoded_key, azure_storage_key);
    unsigned char* digest = HMAC(EVP_sha256(), base64_decoded_key, base64_decoded_key_len, (unsigned char*)string_to_sign, strlen(string_to_sign), NULL, NULL);
    const char* base64_encoded_signature = apr_palloc(pool, APR_ALIGN_DEFAULT(apr_base64_encode_len(SHA256_DIGEST_LENGTH)));
    apr_base64_encode((char*)base64_encoded_signature, (char*)digest, SHA256_DIGEST_LENGTH);
    return apr_pstrcat(pool, "Authorization: ", AUTHORIZATION, " ", azure_storage_account, ":", base64_encoded_signature, NULL);
}

int main(int argc, char *argv[]) {

    apr_status_t rv;
    char err_msg[256];

    rv = apr_initialize();
    if (rv != APR_SUCCESS) {
        //return APR_EGENERAL;
        DIE("APR apr_initialize err.\n");
    }

    apr_pool_create(&pool, NULL);

    //TODO: Resolve debug macro...
    /*
#ifdef DEBUG
    for(int i = 1; i < argc; i++) {
        printf("%s  -- strncmp(\"--key\", argv[i], 5) is %d\n", argv[i], strncmp("--key",argv[i], 6));
    }
    printf("\n");
#endif*/

    cloud_configuration *conf = (cloud_configuration*) apr_pcalloc(pool, APR_ALIGN_DEFAULT( sizeof(cloud_configuration) ));

    char* msc_action = getenv("MCS_ACTION");
    if (msc_action) {
        if(strncasecmp(msc_action, "READ_BLOB", 9) == 0) {
            conf->action = READ_BLOB;
        } else if(strncasecmp(msc_action, "WRITE_BLOB", 10) == 0) {
            conf->action = WRITE_BLOB;
        } else if(strncasecmp(msc_action, "LIST_BLOBS", 10) == 0) {
            conf->action = LIST_BLOBS;
        } else if(strncasecmp(msc_action, "CREATE_CONTAINER", 16) == 0) {
            conf->action = CREATE_CONTAINER;
        } else {
            conf->action = UNDEFINED;
        }
    } else {
        conf->action = UNDEFINED;
    }

    conf->azure_storage_key = getenv("MCS_AZURE_STORAGE_KEY");
    conf->azure_storage_account = getenv("MCS_AZURE_STORAGE_ACCOUNT");
    conf->blob_name = getenv("MCS_AZURE_BLOB_NAME");
    conf->azure_container = getenv("MCS_AZURE_CONTAINER");
    conf->path_to_file = getenv("MCS_PATH_TO_FILE");
    conf->blob_store_url = getenv("MCS_BLOB_STORE_URL");
    char* test_regime = getenv("MCS_TEST_REGIME");
    if (test_regime && strncasecmp("true", test_regime, 4) == 0) {
        conf->action = conf->action | TEST_REGIME;
    }

    for(int i = 1; i < argc; i++) {
        if(strncmp("--action", argv[i], 8) == 0  && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            printf("The value corresponding to --action is %s\n", argv[i+1]);
            if (argv[i+1]) {
                if(strncasecmp(argv[i+1], "READ_BLOB", 9) == 0) {
                    conf->action = READ_BLOB | (conf->action & TEST_REGIME);
                } else if(strncasecmp(argv[i+1], "WRITE_BLOB", 10) == 0) {
                    conf->action = WRITE_BLOB | (conf->action & TEST_REGIME);
                } else if(strncasecmp(argv[i+1], "LIST_BLOBS", 10) == 0) {
                    conf->action = LIST_BLOBS | (conf->action & TEST_REGIME);
                } else if(strncasecmp(argv[i+1], "CREATE_CONTAINER", 16) == 0) {
                    conf->action = CREATE_CONTAINER | (conf->action & TEST_REGIME);
                }
            }
            i++;
        }
        if(strncmp("--azure_storage_key", argv[i], 19) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --azure_storage_key is %s\n", argv[i+1]);
            conf->azure_storage_key = argv[i+1];
            i++;
        }
        if(strncmp("--azure_storage_account", argv[i], 23) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --azure_storage_account is %s\n", argv[i+1]);
            conf->azure_storage_account = argv[i+1];
            i++;
        }
        if(strncmp("--blob_name", argv[i], 11) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --blob_name is %s\n", argv[i+1]);
            conf->blob_name = argv[i+1];
            i++;
        }
        if(strncmp("--azure_container", argv[i], 17) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --azure_container is %s\n", argv[i+1]);
            conf->azure_container = argv[i+1];
            i++;
        }
        if(strncmp("--path_to_file", argv[i], 14) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --path_to_file is %s\n", argv[i+1]);
            conf->path_to_file = argv[i+1];
            i++;
        }
        if(strncmp("--blob_store_url", argv[i], 14) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --blob_store_url is %s\n", argv[i+1]);
            conf->blob_store_url = argv[i+1];
            i++;
        }
        if(strncmp("--test_regime", argv[i], 13) == 0 && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            DEBUG_MSG("The value corresponding to --test_regime is %s\n", argv[i+1]);
            if (argv[i+1] && strncasecmp("true", argv[i+1], 4) == 0) {
                conf->action = conf->action | TEST_REGIME;
            } else {
                conf->action = conf->action & ~TEST_REGIME;
            }
            i++;
        }
    }

    if(conf->action & UNDEFINED) {
        DIE("Action must be specified either as an env var MCS_ACTION or as a parameter --action with possible values: <READ_BLOB|WRITE_BLOB|LIST_BLOBS|CREATE_CONTAINER>\n");
    }

    // TODO: 512 bit, base64, 84 ish + padding?... Smaller cannot be valid.
    if(!conf->azure_storage_key || strlen(conf->azure_storage_key) < 80) {
        DIE("Azure_storage_key must be specified either as an env var MCS_AZURE_STORAGE_KEY or as a parameter --azure_storage_key with its string value being at least 80 characters long.\n");
    }

    if(!conf->azure_storage_account || strlen(conf->azure_storage_account) < 2) {
        DIE("Azure_storage_account must be specified either as an env var MCS_AZURE_STORAGE_ACCOUNT or as a parameter --azure_storage_account with its string value being at least 2 characters long.\n");
    }

    if((conf->action & READ_BLOB || conf->action & WRITE_BLOB) && (!conf->blob_name || strlen(conf->blob_name) < 1)) {
        DIE("Blob_name must be specified either as an env var MCS_AZURE_BLOB_NAME or as a parameter --blob_name with its string value being at least 1 character long.\n");
    }

    if(!conf->azure_container || strlen(conf->azure_container) < 2) {
        DIE("Azure_container must be specified either as an env var MCS_AZURE_CONTAINER or as a parameter --azure_container with its string value being at least 2 characters long.\n");
    }

    if(!conf->blob_store_url || strlen(conf->blob_store_url) < 8) {
        // Set a reasonable default: https://docs.microsoft.com/en-us/azure/storage/common/storage-create-storage-account#storage-account-endpoints
        conf->blob_store_url= "blob.core.windows.net";
    }

    if(strncmp(conf->blob_store_url, "http", 4) == 0) {
        ERR_MSG("Warning: We use Blob_store_url without schema, i.e. expected values are for example: blob.core.windows.net or 127.0.0.1:10000 etc. Please, check your MCS_BLOB_STORE_URL env var and --blob_store_url parameter. Current value: %s\n", conf->blob_store_url);
    }

    if(conf->action & TEST_REGIME && strstr(conf->blob_store_url, "blob.core.windows.net")) {
        ERR_MSG("Warning: TEST_REGIME is enabled and yet the blob_store_url seems to point to the production blob store blob.core.windows.net: %s\n", conf->blob_store_url);
    }

    if(!(conf->action & TEST_REGIME) && !strstr(conf->blob_store_url, "blob.core.windows.net")) {
        ERR_MSG("Warning: TEST_REGIME is not enabled and yet the blob_store_url seems to point to something else than blob.core.windows.net: %s\n", conf->blob_store_url);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        // Common variables
        const char* url;
        const char* request_method;
        char request_date[APR_RFC822_DATE_LEN];
        apr_rfc822_date(request_date,apr_time_now());
        // TODO: Probably could remain fixed. The API version is intimatelydd
        const char* storage_service_version="2016-05-31";
        // https://docs.microsoft.com/en-us/rest/api/storageservices/put-blob#request-headers-all-blob-types
        const char* client_request_id = "mod_cloud_storage";
        // HTTP Request headers
        const char* x_ms_date_h = apr_pstrcat(pool, "x-ms-date:", request_date, NULL);
        const char* x_ms_version_h = apr_pstrcat(pool, "x-ms-version:", storage_service_version, NULL);
        const char* x_ms_client_request_id_h = apr_pstrcat(pool,"x-ms-client-request-id:", client_request_id, NULL);

        const char* canonicalized_headers;
        const char* canonicalized_resource;
        // See for all those \n: https://docs.microsoft.com/en-us/rest/api/storageservices/authentication-for-the-azure-storage-services
        char* string_to_sign;

        // Writing/Reading Blobs
        const char* x_ms_blob_content_type_h = "x-ms-blob-content-type:application/octet-stream";
        const char* x_content_type_h = "Content-Type:application/octet-stream";
        const char* x_ms_blob_type_h = "x-ms-blob-type:BlockBlob";
        char* x_content_length_h = "Content-Length:0";
        const char* x_ms_blob_content_md5;
        const char* file_length_str;
        unsigned char file_md5sum_digest[APR_MD5_DIGESTSIZE];
        const char* file_md5sum_base64;
        apr_file_t *filep = NULL;
        apr_finfo_t file_info;
        char* file_data;

        // cURL headers
        headers = curl_slist_append(headers, x_ms_date_h);
        curl_slist_append(headers, x_ms_version_h);
        curl_slist_append(headers, x_ms_client_request_id_h);

        // ACTIONS
        if(conf->action & CREATE_CONTAINER || conf->action & WRITE_BLOB) {
            request_method="PUT";
        } else {
            request_method="GET";
        }

        if(conf->action & CREATE_CONTAINER) {
            canonicalized_headers = apr_pstrcat(pool, x_ms_client_request_id_h, "\n", x_ms_date_h, "\n", x_ms_version_h, NULL);
            canonicalized_resource = apr_pstrcat(pool, "/", conf->azure_storage_account, "/", conf->azure_container, "\nrestype:container", NULL);
            string_to_sign = apr_pstrcat(pool, request_method,"\n\n\n\n\n\n\n\n\n\n\n\n", canonicalized_headers, "\n", canonicalized_resource, NULL);

            if(conf->action & TEST_REGIME) {
                url = apr_pstrcat(pool, "http://", conf->blob_store_url, "/", conf->azure_storage_account, "/", conf->azure_container, "?restype=container", NULL);
            } else {
                url = apr_pstrcat(pool, "https://", conf->azure_storage_account, ".", conf->blob_store_url, "/", conf->azure_container, "?restype=container", NULL);
            }
        } else if(conf->action & LIST_BLOBS) {
            canonicalized_headers = apr_pstrcat(pool, x_ms_client_request_id_h, "\n", x_ms_date_h, "\n", x_ms_version_h, NULL);
            canonicalized_resource = apr_pstrcat(pool, "/", conf->azure_storage_account, "/", conf->azure_container, "\ncomp:list\nrestype:container", NULL);
            string_to_sign = apr_pstrcat(pool, request_method,"\n\n\n\n\n\n\n\n\n\n\n\n", canonicalized_headers, "\n", canonicalized_resource, NULL);

            if(conf->action & TEST_REGIME) {
                url = apr_pstrcat(pool, "http://", conf->blob_store_url, "/", conf->azure_storage_account, "/", conf->azure_container, "?restype=container&comp=list", NULL);
            } else {
                url = apr_pstrcat(pool, "https://", conf->azure_storage_account, ".", conf->blob_store_url, "/", conf->azure_container, "?restype=container&comp=list", NULL);
            }
        } else if(conf->action & WRITE_BLOB) {
            if(conf->path_to_file && strlen(conf->path_to_file) > 0) {
                rv = apr_file_open(&filep, conf->path_to_file, APR_READ|APR_BUFFERED, APR_OS_DEFAULT, pool);
                if (rv != APR_SUCCESS) {
                    DIE("Path to file %s was specified, but it doesn't exist or cannot be read.\n", conf->path_to_file);
                }
                DEBUG_MSG("Reading from file.\n");
            } else {
                rv = apr_file_open_stdin(&filep, pool);
                DEBUG_MSG("Reading from stdin\n");
                if (rv != APR_SUCCESS) {
                    DIE("Cannot open stdin and neither MCS_PATH_TO_FILE env var nor --path_to_file was specified.\n");
                }
            }
            if (apr_file_info_get(&file_info, APR_FINFO_SIZE, filep) == APR_SUCCESS) {
                DEBUG_MSG("BLOB to be uploaded length: %ld\n", file_info.size);
                if(file_info.size < 1) {
                    DIE("Neither a file on path specified by MCS_PATH_TO_FILE env var nor --path_to_file nor stdin contains any bytes to read. Empty input?\n");
                }
            }
            // TODO: Verify if pertinent...
            //       head -c256  /dev/urandom | xxd -p -c256 > /home/karm/Projects/mod_cloud_storage/testdata
            //if(fi.size < 512) {
            //    ERR_MSG("Warning: The BLOB to be written is only %ld long. The smallest write operation is 512B. The rest of the blob will be zeroes.\n", file_info.size);
            //}
            file_data = apr_palloc(pool, file_info.size);
            rv = apr_file_read_full(filep, file_data, file_info.size, &file_length);
            if (rv != APR_SUCCESS) {
                if (rv != APR_SUCCESS) {
                    DIE("Cannot read data to create the BLOB. Check your input, please. We expected %ld bytes to read.\n", file_info.size);
                }
            }
            apr_file_close(filep);
            rv = apr_md5(file_md5sum_digest, file_data, file_length);
            if (rv != APR_SUCCESS) {
                DIE("Couldn't create MD5 hashsum from %ld bytes of BLOB input. This should never happen.", file_info.size);
            }
            file_md5sum_base64 = apr_palloc(pool, APR_ALIGN_DEFAULT(apr_base64_encode_len(APR_MD5_DIGESTSIZE)));

            apr_base64_encode((char*)file_md5sum_base64, (char*)file_md5sum_digest, APR_MD5_DIGESTSIZE);

            DEBUG_MSG("BLOB to be uploaded MD5sum base64: %s\n", file_md5sum_base64);
            DEBUG_MSG("BLOB to be uploaded MD5sum hex:    %s\n", apr_pescape_hex(pool, file_md5sum_digest, APR_MD5_DIGESTSIZE, 0));

            file_length_str = apr_ltoa(pool, file_length);
            x_content_length_h = apr_pstrcat(pool, "Content-Length:", file_length_str, NULL);
            x_ms_blob_content_md5 = apr_pstrcat(pool, "x-ms-blob-content-md5:", file_md5sum_base64, NULL);

            canonicalized_headers = apr_pstrcat(pool, x_ms_blob_content_md5, "\n", x_ms_blob_content_type_h, "\n", x_ms_blob_type_h, "\n", x_ms_client_request_id_h, "\n", x_ms_date_h, "\n", x_ms_version_h, NULL);
            canonicalized_resource = apr_pstrcat(pool, "/", conf->azure_storage_account, "/", conf->azure_container, "/", conf->blob_name, NULL);
            string_to_sign = apr_pstrcat(pool, request_method, "\n\n\n", file_length_str, "\n\napplication/octet-stream\n\n\n\n\n\n\n", canonicalized_headers, "\n", canonicalized_resource, NULL);

            if(conf->action & TEST_REGIME) {
                url = apr_pstrcat(pool, "http://", conf->blob_store_url, "/", conf->azure_storage_account, "/", conf->azure_container, "/", conf->blob_name, NULL);
            } else {
                url = apr_pstrcat(pool, "https://", conf->azure_storage_account, ".", conf->blob_store_url, "/", conf->azure_container, "/", conf->blob_name, NULL);
            }
            curl_slist_append(headers, x_content_type_h);
            curl_slist_append(headers, x_ms_blob_content_type_h);
            curl_slist_append(headers, x_ms_blob_type_h);
            curl_slist_append(headers, x_ms_blob_content_md5);
        } else if(conf->action & READ_BLOB) {
            canonicalized_headers = apr_pstrcat(pool, x_ms_client_request_id_h, "\n", x_ms_date_h, "\n", x_ms_version_h, NULL);
            canonicalized_resource = apr_pstrcat(pool, "/", conf->azure_storage_account, "/", conf->azure_container, "/", conf->blob_name, NULL);
            string_to_sign = apr_pstrcat(pool, request_method,"\n\n\n\n\n\n\n\n\n\n\n\n", canonicalized_headers, "\n", canonicalized_resource, NULL);

            if(conf->action & TEST_REGIME) {
                url = apr_pstrcat(pool, "http://", conf->blob_store_url, "/", conf->azure_storage_account, "/", conf->azure_container, "/", conf->blob_name, NULL);
            } else {
                url = apr_pstrcat(pool, "https://", conf->azure_storage_account, ".", conf->blob_store_url, "/", conf->azure_container, "/", conf->blob_name, NULL);
            }
        } else {
            DIE("No known action was specified. It should never happen at this point.\n");
        }

        // cURL Call to action...
        curl_slist_append(headers, x_content_length_h);
        curl_slist_append(headers, produce_authorization_header(conf->azure_storage_key, conf->azure_storage_account, string_to_sign));

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        // TODO: Make configurable
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000);
        // TODO: Doesn't make much sense unless we keep the connection, do we want to keep it in the module? Let's make it configurable.
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 10000);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 1000);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request_method);
        if(conf->action & WRITE_BLOB) {
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_READDATA, file_data);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_length);
        }
        curl_easy_setopt(curl, CURLOPT_URL, url);

        // Here we block on the web request
        CURLcode err = curl_easy_perform(curl);

        int http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        const char *str = curl_easy_strerror(err);
        DEBUG_MSG(" libcurl said: %s\n", str);
        //TODO if CURLcode is not CURLE_OK, print some debug info about URL....
        if(err != CURLE_OK) {
            DEBUG_MSG("      URL was: %s\n", url);
        }
        DEBUG_MSG("Response Code: %d\n", http_code);

        if(conf->action & READ_BLOB && conf->path_to_file && strlen(conf->path_to_file) > 0) {
            rv = apr_file_open(&filep, conf->path_to_file, APR_FOPEN_CREATE|APR_FOPEN_WRITE|APR_BUFFERED, APR_OS_DEFAULT, pool);
            if (rv != APR_SUCCESS) {
                DIE("Path to file %s was specified, but it doesn't exist or cannot be opened for writing. Error: %s\n", conf->path_to_file, apr_strerror(rv, err_msg, sizeof(err_msg)));
            }
            DEBUG_MSG("Writing to file: %s\n", conf->path_to_file);
            // This op could block forever
            size_t written_bytes;
            rv = apr_file_write_full(filep, data.response_body, data.size, &written_bytes);
            apr_file_close(filep);
            if (rv != APR_SUCCESS) {
                DIE("Failed to write %ld bytes to the file %s. I/O Error?\n", data.size, conf->path_to_file);
            }
            if(written_bytes != data.size) {
                DIE("This should never happen. Only %ld out of %ld bytes written and yet there was no error. File was: %s.\n?", written_bytes, data.size, conf->path_to_file);
            }
            printf("%ld bytes of response writen to %s.\n", written_bytes, conf->path_to_file);
        } else {
            printf("Response Body followed by \\n:%s\n", data.response_body);
        }

        /*
         TODO: Should we react to specific states or just spit them out?
         e.g.
         Response Code: 409.
         Response Body: <?xml version="1.0" encoding="utf-8"?><Error><Code>ContainerAlreadyExists</Code><Message>The specified container already exists.</Message></Error>.

         or

         Response Body: <?xml version='1.0'?><EnumerationResults ServiceEndpoint="http://localhost:10000/devstoreaccount1" ContainerName="mod-cloud-storage"><Blobs><Blob><Name>test</Name><Properties><BlobType>BlockBlob</BlobType><LeaseStatus>unlocked</LeaseStatus><LeaseState>available</LeaseState><ServerEncrypted>false</ServerEncrypted><Last-Modified>Thu, 24 Aug 2017 15:34:29 GMT</Last-Modified><ETag>0</ETag><Content-Type>application/octet-stream</Content-Type><Content-Encoding>undefined</Content-Encoding><Content-MD5>VA/zHcYvgkcsRR9LRKbv2Q==</Content-MD5></Properties></Blob></Blobs><BlobPrefix><name></name></BlobPrefix></EnumerationResults>
        */
    } else {
        DIE("CURL wasn't initialized.\n");
    }

    free_connection();
    apr_pool_destroy(pool);
    apr_terminate();
    return 0;
}
