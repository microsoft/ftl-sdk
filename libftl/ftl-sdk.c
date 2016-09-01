
#include "ftl.h"
#include "ftl_private.h"

FTL_API ftl_status_t ftl_init(){
  return FTL_SUCCESS;
}

FTL_API ftl_handle_t* ftl_ingest_create(ftl_ingest_params_t *params){
  ftl_stream_configuration_private_t *ftl_cfg;

  if( (cfg = (ftl_stream_configuration_private_t *)malloc(sizeof(ftl_stream_configuration_private_t)) == NULL){
    return NULL;
  }

  memcpy(cfg->params, params, sizeof(ftl_ingest_params_t));
}

FTL_API ftl_status_t ftl_ingest_connect(ftl_handle_t *ftl_handle){
  ftl_stream_configuration_private_t *ftl_cfg = (ftl_stream_configuration_private_t *)ftl_handle->private;


}

FTL_API ftl_status_t ftl_ingest_get_status(ftl_handle_t *ftl_handle);

FTL_API ftl_status_t ftl_ingest_update_hostname(ftl_handle_t *ftl_handle, const char *ingest_hostname);
FTL_API ftl_status_t ftl_ingest_update_stream_key(ftl_handle_t *ftl_handle, const char *stream_key);

FTL_API ftl_status_t ftl_ingest_send_media(ftl_handle_t *ftl_handle, ftl_media_type_t media_type, uint8_t *data, int32 len);

FTL_API ftl_status_t ftl_ingest_disconnect(ftl_handle_t *ftl_handle);

FTL_API ftl_status_t ftl_ingest_destroy(ftl_handle_t *ftl_handle);