#include "image_plugin_service.h"

int databind_5_Image_5_Codec_6_Decode(
    const DecodeRequest_t *request,
    DecodeResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->pixels = request->width * 4u;
  return 0;
}

int databind_5_Image_5_Codec_6_Encode(
    const EncodeRequest_t *request,
    EncodeResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->bytes = request->pixels / 4u;
  return 0;
}
