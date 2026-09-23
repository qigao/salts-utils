#include "image.generated.h"

int Image_Codec_Decode(
    const DecodeRequest_t *request,
    DecodeResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->width = request->id * 2u;
  return 0;
}

int Image_Codec_Encode(
    const EncodeRequest_t *request,
    EncodeResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->bytes = request->quality + 100u;
  return 0;
}
