// serviceHandler.ts
const { Config } = require('./proto/configuration_pb');
const { proto } = require('./proto/DataRequest_pb');


function sendRequest(type: proto.sys.request.Type, action: proto.sys.request.Action, data: any, onSuccess: (response: any) => void, onError: (error: any) => void) {
  let payload:proto.sys.request.Payload = new proto.sys.request.Payload();
  payload.setType(type);
  payload.setAction(action);
  switch (type) {
    case proto.sys.request.Type.CONFIG:
        payload.setConfig(data);
        break;
        case proto.sys.request.Type.OTA:
        payload.setUrl(data);
        break; 
    default:
        break;
  }
  
 let serializedPayload = payload.serializeBinary();
 console.log(`Sending data: ${serializedPayload}`)

  $.ajax({
    url: '/data.bin',
    method: 'POST',
    contentType: 'application/octet-stream',
    processData: false,
    data: serializedPayload,
    success: function(responseData) {
      try {
        // Deserialize response here or pass raw data to onSuccess
        let response = proto.sys.request.Response.deserializeBinary(new Uint8Array(responseData));
        onSuccess(response);
      } catch (error) {
        console.error('Error decoding protobuf message:', error);
        onError(error);
      }
    },
    error: function(jqXHR, textStatus, errorThrown) {
      console.error('Error in request:', textStatus, errorThrown);
      onError(errorThrown);
    }
  }).fail(function (xhr, ajaxOptions, thrownError) {
    handleExceptionResponse(xhr, ajaxOptions, thrownError);
  });
}

export { sendRequest };
