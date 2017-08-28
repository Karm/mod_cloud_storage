/*
 * @author Michal Karm Babacek
 *
 * See LICENSE
 * TODO
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include <apr_time.h>
#include <apr_file_io.h>
#include <apr_pools.h>
#include <apr_file_info.h>

#include <curl/curl.h>

// TODO: Content types?
#define CONTENT_TYPE "Content-Type: application/octet-stream"
// TODO: CMake injected version?
#define USER_AGENT   "Hahahaha! 0.0"

//TODO: Isn't it just stupid to assume 1K? Make it configurable? LIST_BLOBS XML response listing 1 blob called "test" is 630 bytes long...
#define INITIAL_RESPONSE_MEM 1024

//TODO: Error and Debug messages...
#define DEBUG 1
#define DIE(X, ...) fprintf(stderr, "ERROR %s:%d: " X "\n", __FILE__, __LINE__, ##__VA_ARGS__); exit(EXIT_FAILURE);
#define ERR_MSG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define DEBUG_MSG(fmt, ...) do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

CURL *curl;
CURLcode res;
struct my_response data;
FILE *headerfile;
struct curl_slist *headers = NULL;
apr_pool_t *pool;

struct my_response {
    size_t size;
    char* response_body;
};

enum {
    UNDEFINED        = 0x0010,
    READ_BLOB        = 0x0020,
    WRITE_BLOB       = 0x0040,
    LIST_BLOBS       = 0x0080,
    CREATE_CONTAINER = 0x0160
};

struct cloud_configuration {
    // Credentials.
    char* azure_storage_key;
    char* azure_storage_account;
    // Container to be used.
    char* azure_container;
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
    struct my_response *mem = (struct my_response *)userp;

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


int main(int argc, char *argv[]) {

    apr_status_t rv;
    //apr_file_t *fp;

    rv = apr_initialize();
    if (rv != APR_SUCCESS) {
        //return APR_EGENERAL;
        DIE("APR apr_initialize err.\n");
    }

    apr_pool_create(&pool, NULL);

    //TODO: Resolve debug macro...
#ifdef DEBUG
    for(int i = 1; i < argc; i++) {
        printf("%s  -- strncmp(\"--key\", argv[i], 5) is %d\n", argv[i], strncmp("--key",argv[i], 6));
    }
    printf("\n");
#endif

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

    for(int i = 1; i < argc; i++) {
        if(strncmp("--action", argv[i], 8) == 0  && i+1 < argc && strncmp("--", argv[i+1], 2) < 0) {
            printf("The value corresponding to --action is %s\n", argv[i+1]);
            if (argv[i+1]) {
                if(strncasecmp(argv[i+1], "READ_BLOB", 9) == 0) {
                    conf->action = READ_BLOB;
                } else if(strncasecmp(argv[i+1], "WRITE_BLOB", 10) == 0) {
                    conf->action = WRITE_BLOB;
                } else if(strncasecmp(argv[i+1], "LIST_BLOBS", 10) == 0) {
                    conf->action = LIST_BLOBS;
                } else if(strncasecmp(argv[i+1], "CREATE_CONTAINER", 16) == 0) {
                    conf->action = CREATE_CONTAINER;
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
    }

    if(conf->action & UNDEFINED) {
        DIE("Action must be specified either as an env var MCS_ACTION or as a parameter --action with possible values: <READ_BLOB|WRITE_BLOB|LIST_BLOBS>\n");
    }

    // TODO: 512 bit, base64, 84 ish + padding...?
    if(!conf->azure_storage_key || strlen(conf->azure_storage_key) < 80) {
        DIE("Azure_storage_key must be specified either as an env var MCS_AZURE_STORAGE_KEY or as a parameter --azure_storage_key with its string value being at least 80 characters long.\n");
    }

    if(!conf->azure_storage_account || strlen(conf->azure_storage_account) < 2) {
        DIE("Azure_storage_account must be specified either as an env var MCS_AZURE_STORAGE_ACCOUNT or as a parameter --azure_storage_account with its string value being at least 2 characters long.\n");
    }

    if(!conf->blob_name || strlen(conf->blob_name) < 1) {
        DIE("Blob_name must be specified either as an env var MCS_AZURE_BLOB_NAME or as a parameter --blob_name with its string value being at least 1 character long.\n");
    }

    if(!conf->azure_container || strlen(conf->azure_container) < 2) {
        DIE("Azure_container must be specified either as an env var MCS_AZURE_CONTAINER or as a parameter --azure_container with its string value being at least 2 characters long.\n");
    }

    /*if (conf->action  & LIST_BLOBS) {
        printf("Action: LIST_BLOBS\n");
    }
    if (conf->action  & READ_BLOB) {
        printf("Action: READ_BLOB\n");
    }*/

    //return 0;

    data.size = 0;
    data.response_body = apr_pcalloc(pool, APR_ALIGN_DEFAULT(INITIAL_RESPONSE_MEM));
    if(NULL == data.response_body) {
        //return APR_EGENERAL;
        DIE("Failed to allocate memory.\n");
    }


    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 10000);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 1000);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);


        // TODO: Headers and URL
        headers = curl_slist_append(headers, CONTENT_TYPE);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, "https://karms.biz");


        res = curl_easy_perform(curl);
        int http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        DEBUG_MSG("Response Code: %d.\n", http_code);
        DEBUG_MSG("Response Body: %s.\n", data.response_body);
    } else {
        DIE("CURL wasn't initialized.\n");
    }

    free_connection();
    //apr_file_close(fp);
    apr_pool_destroy(pool);
    apr_terminate();
    return 0;
}
