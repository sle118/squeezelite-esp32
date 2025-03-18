

function handleExceptionResponse(xhr: JQuery.jqXHR<any>
    , _ajaxOptions: JQuery.Ajax.ErrorTextStatus, thrownError: string) {
    console.log(xhr.status);
    console.log(thrownError);
    if (thrownError !== '') {
      showLocalMessage(thrownError, 'MESSAGING_ERROR');
    }
  }

  