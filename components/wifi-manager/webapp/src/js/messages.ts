

let messagecount = 0;

type MessageEntry = {
    type: string;
    message: string;
    class: string;
    sent_time: number;
    current_time: number;
  };
function showMessage(msg: MessageEntry, msgTime: Date,messageseverity: string= 'MESSAGING_INFO') {
    let color = 'table-success';
  
    if (msg.type === 'MESSAGING_WARNING') {
      color = 'table-warning';
      if (messageseverity === 'MESSAGING_INFO') {
        messageseverity = 'MESSAGING_WARNING';
      }
    } else if (msg.type === 'MESSAGING_ERROR') {
      if (
        messageseverity === 'MESSAGING_INFO' ||
        messageseverity === 'MESSAGING_WARNING'
      ) {
        messageseverity = 'MESSAGING_ERROR';
      }
      color = 'table-danger';
    }
    if (++messagecount > 0) {
      $('#msgcnt').removeClass('badge-success');
      $('#msgcnt').removeClass('badge-warning');
      $('#msgcnt').removeClass('badge-danger');
      $('#msgcnt').addClass({
        MESSAGING_INFO: 'badge-success',
        MESSAGING_WARNING: 'badge-warning',
        MESSAGING_ERROR: 'badge-danger',
      }[messageseverity]);
      $('#msgcnt').text(messagecount);
    }
  
    $('#syslogTable').append(
      `<tr class='${color}'><td>${msgTime.toLocalShort()}</td><td>${msg.message.encodeHTML()}</td></tr>`
    );
  }
function showLocalMessage(message: string, severity: string) {
    const msg: MessageEntry = {
      message: message,
      type: severity,
      class: '',
      sent_time: 0,
      current_time: 0
    };
    showMessage(msg, new Date());
  }