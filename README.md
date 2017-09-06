# mod_cloud_storage
Storing and reading BLOBs to and from a cloud service.

# Building
 * cmake
 * APR, APR Util
 * OpenSSL

# Releases and Downloads

See [Releases](https://github.com/Karm/mod_cloud_storage/releases)

# Usage

The command line tool is controlled via env variables and command line arguments. Command line arguments take priority and overwrite env variables settings.

## Env variables
```
MCS_ACTION=READ_BLOB|WRITE_BLOB|LIST_BLOBS|CREATE_CONTAINER
MCS_AZURE_STORAGE_KEY=Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==
MCS_AZURE_STORAGE_ACCOUNT=devstoreaccount1
MCS_AZURE_BLOB_NAME=test
MCS_AZURE_CONTAINER=your-container
MCS_PATH_TO_FILE=/tmp/meh
MCS_BLOB_STORE_URL=127.0.0.1:10000
MCS_TEST_REGIME=true|false
```

## Command line arguments
```
--action
--azure_storage_key
--azure_storage_account
--blob_name
--azure_container
--path_to_file
--blob_store_url
--test_regime
```

*Only LIST_BLOBS and CREATE_CONTAINER are implemented at the moment.*

## Testing and fooling around
 * Get your [Azurite container up and running](https://github.com/arafato/azurite#docker-image):

 ```
./mod_cloud_storage \
--action CREATE_CONTAINER \
--azure_storage_key Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw== \
--azure_storage_account devstoreaccount1 \
--azure_container my-first-container \
--blob_store_url 127.0.0.1:10000 \
--test_regime true
```

## Working with real Azure storage
 * See Azure Portal and Azure Docs on creating Storage Account
 * Copy your access key from your Azure Portal
 * set test_regime to false or leave it blank
 * set blob_store_url to blob.core.windows.net
 * set azure_storage_account to your actual storage account name
 * *Do not use a storage account with valuable containers in it. This tool is just a toy.*
