#include <stdio.h>
#include <string.h>

#include "../../src/readalike_prepr/phda9_preprocess.h"

int main(int argc, char** argv) {
  if (argc != 4 || (strcmp(argv[1], "encode") != 0
      && strcmp(argv[1], "decode") != 0)) {
    fprintf(stderr, "usage: phda9_tool encode|decode input output\n");
    return 1;
  }

  FILE* input = fopen(argv[2], "rb");
  FILE* output = fopen(argv[3], "wb");
  if (input == nullptr || output == nullptr) {
    fprintf(stderr, "failed to open input or output\n");
    if (input != nullptr) fclose(input);
    if (output != nullptr) fclose(output);
    return 1;
  }

  if (strcmp(argv[1], "encode") == 0) {
    encode_txt_wit(input, output);
  } else {
    fseek(input, 0, SEEK_END);
    U64 size = ftell(input);
    fseek(input, 0, SEEK_SET);
    decode_txt_wit(input, output, size);
  }

  fclose(input);
  fclose(output);
  return 0;
}
