#include "installed_message_native.h"
#include <cmeta/data.h>
#include <data_bind_message_plan.h>
#include <data_bind_native_binding.h>
#include <string.h>

int main(void) {
  const DataBindMessageNativeArtifact *artifact = Config_native_artifact();
  const DataBindMessageNativeArtifact *sequence_artifact =
      HeaderPolicy_native_artifact();
  DataBindNativeTypeBinding binding = {0};
  DataBindNativeTypeBinding sequence_binding = {0};
  DataBindMessagePlan *plan = NULL;
  DataBindMessagePlanDiagnostic diagnostic =
      DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBind *codec = NULL;
  const cmeta_data_struct_shape *shape;
  const cmeta_data_desc *sequence;
  const cmeta_data_desc *element;
  int rc = 0;

  if (!data_bind_message_native_artifact_valid(artifact)) return 1;
  if (strcmp(artifact->type_name, "Config") != 0) return 2;
  if (artifact->native_binding(&binding, &error) != DATA_BIND_OK) return 3;
  if (binding.data == NULL || binding.idl_type_name == NULL) return 4;

  if (!data_bind_message_native_artifact_valid(sequence_artifact)) return 5;
  if (sequence_artifact->native_binding(&sequence_binding, &error) !=
      DATA_BIND_OK)
    return 6;
  if (sequence_binding.data == NULL ||
      sequence_binding.data->kind != CMETA_DATA_STRUCT ||
      sequence_binding.data->shape == NULL)
    return 7;

  shape = (const cmeta_data_struct_shape *)sequence_binding.data->shape;
  if (shape->field_count != 2u || shape->fields[1].value == NULL) return 8;
  sequence = shape->fields[1].value;
  if (sequence->kind != CMETA_DATA_SEQUENCE ||
      cmeta_data_collection_ops_of(sequence) == NULL ||
      cmeta_data_construct_ops_of(sequence) == NULL)
    return 9;

  element = cmeta_data_collection_element_data(sequence);
  if (element == NULL || element->kind != CMETA_DATA_STRUCT ||
      element->storage_type == NULL ||
      cmeta_type_require_traits(
          element->storage_type,
          CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) !=
          CMETA_OK)
    return 10;

  if (InstalledMessage_codec_create(&codec, &error) != DATA_BIND_OK ||
      codec == NULL)
    return 11;
  if (data_bind_message_plan_compile(
          codec, "HeaderPolicy", &sequence_binding, &plan, &diagnostic) !=
      DATA_BIND_OK ||
      plan == NULL)
    rc = 12;

  data_bind_message_plan_free(plan);
  data_bind_free(codec);
  return rc;
}
