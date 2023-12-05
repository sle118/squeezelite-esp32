var he = require('he');
// var Promise = require('es6-promise').Promise;

// @ts-ignore
import Cookies from 'js-cookie';
var protobuf = require("protobufjs");
import * as bootstrap from 'bootstrap';





declare global {
  interface Window {
    hideSurrounding: (obj: HTMLElement) => void;
    hFlash: () => void;
    handleReboot: (link: string) => void;
    setURL: (button: HTMLButtonElement) => void;
    runCommand: (button: HTMLButtonElement, reboot: boolean) => void;
  }
  interface String {
    format(...args: any[]): string;
    encodeHTML(): string;
  }
  interface Date {
    toLocalShort(): string;
  }
}
interface Field {
  value: string;
  attributes: {
    cmdname: {
      value: string;
    };
  };
}
type GPIOEntry = {
  fixed: any;
  group: string;
  name: string;
  gpio: string;
};
type BTDevice = {
  name: string;
  rssi: number;
};
interface ArgTableEntry {
  glossary: string;
  longopts: string;
  checkbox: boolean;
  remark: boolean;
  hasvalue: boolean;
  mincount: number;
  maxcount: number;
  datatype?: string;
  shortopts?: string;
}
type MessageEntry = {
  type: string;
  message: string;
  class: string;
  sent_time:number;
  current_time:number;
};
interface CommandEntry {
  help: string;
  hascb: boolean;
  name: string;
  argtable?: ArgTableEntry[];
  hint?: string;
}
type ReleaseEntry = {
  assets: {
    browser_download_url: string;
    name: string
  }[];
  body: any;
  created_at: string | number | Date;
  name: string;
};
type OutputType = 'i2s' | 'spdif' | 'bt';
interface TaskDetails {
  nme: string; cpu: number; st: number; minstk: number; bprio: number; cprio: number; num: number;
}
interface ConfigValue {
  value: string | number;
  type: number;
}

interface ConfigPayload {
  timestamp: number;
  config: { [key: string]: ConfigValue };
}
interface NetworkConnection {
  urc: number;
  auth: number ;
  pwd: string ;
  dhcpname: string ;
  Action: number ;
  ip: string ;
  ssid: string ;
  rssi: number ;
  gw: string ;
  netmask: string ;
}
interface StatusObject {
  project_name?: string;
  version?: string;
  recovery?: number;
  Jack?: string;
  Voltage?: number;
  disconnect_count?: number;
  avg_conn_time?: number;
  is_i2c_locked?: boolean;
  urc?: number;
  bt_status?: number;
  bt_sub_status?: number;
  rssi?: number;
  ssid?: string;
  ip?: string;
  netmask?: string;
  gw?: string;
  lms_cport?: number;
  lms_port?: number;
  lms_ip?: string;
  if?: string;
  platform_name?: string;
  depth?: number;
  loaded?: number;
  total?: number;
  ota_pct?: number;
  ota_dsc?: string;
}
// Function to reset a StatusObject
function resetStatusObject(): StatusObject {
  return {
    netmask: undefined,
    ip: undefined,
    ssid: undefined,
    urc: undefined,
    rssi: undefined,
    gw: undefined,
    bt_status: undefined,
    bt_sub_status: undefined,
    loaded: undefined,
    total: undefined,
    ota_pct: undefined,
    ota_dsc: undefined,
    recovery: undefined
  };
}
function resetNetworkConnection(): NetworkConnection {
  return {
    auth: undefined,
    pwd: undefined,
    dhcpname: undefined,
    Action: undefined,
    ip: undefined,
    ssid: undefined,
    rssi: undefined,
    gw: undefined,
    netmask: undefined,
    urc: 0
  };
}

window.hideSurrounding = function (obj: HTMLElement): void {
  $(obj).parent().parent().hide();
}
function hFlash() {
  // reset file upload selection if any;
  $('#flashfilename').val = null
  flashState.StartOTA();
}
window.hFlash = function () {
  hFlash()
}
function handleReboot(link: string) {
  if (link == 'reboot_ota') {
    $('#reboot_ota_nav').removeClass('active').prop("disabled", true); delayReboot(500, '', 'reboot_ota');
  }
  else {
    $('#reboot_nav').removeClass('active'); delayReboot(500, '', link);
  }
}
window.handleReboot = function (link) {
  handleReboot(link);
}

Object.assign(String.prototype, {
  format(...args: any[]): string {
    return this.replace(/{(\d+)}/g, function (match: string, number: string) {
      const index = parseInt(number, 10); // Convert string to number
      return typeof args[index] !== 'undefined' ? args[index] : match;
    });
  },
});

Object.assign(String.prototype, {
  encodeHTML() {
    return he.encode(this).replace(/\n/g, '<br />');
  },
});

Object.assign(Date.prototype, {
  toLocalShort() {
    const opt: Intl.DateTimeFormatOptions = { dateStyle: 'short', timeStyle: 'short' };
    return this.toLocaleString(undefined, opt);

  },
});

function get_control_option_value(obj: (string | JQuery<HTMLElement> | HTMLElement | HTMLInputElement)): { opt: string, val: string | boolean | number } {
  let ctrl, id, val, opt;
  if (typeof (obj) === 'string') {
    id = obj;
    ctrl = $(`#${id}`);
  } else {
    id = $(obj).attr('id');
    ctrl = $(obj);
  }
  if (ctrl.attr('type') === 'checkbox') {
    opt = ctrl.prop('checked') ? id.replace('cmd_opt_', '') : '';
    val = true;
  }
  else {
    opt = id.replace('cmd_opt_', '');
    val = ctrl.val();
    if (typeof val === 'string') {
      val = `${val.includes(" ") ? '"' : ''}${val}${val.includes(" ") ? '"' : ''}`;
    }
    else if (typeof val !== 'number') {
      val = val.toString();
    }
  }

  return { opt, val };
}
function handleNVSVisible() {
  let nvs_previous_checked: boolean = isEnabled(Cookies.get("show-nvs"));
  const checkBoxElement = $('input#show-nvs')[0] as HTMLInputElement;
  checkBoxElement.checked = nvs_previous_checked;

  if (checkBoxElement.checked || recovery) {
    $('*[href*="-nvs"]').show();
  } else {
    $('*[href*="-nvs"]').hide();
  }
}
function concatenateOptions(options: object): string {
  let commandLine = ' ';
  for (const [option, value] of Object.entries(options)) {
    if (option !== 'n' && option !== 'o') {
      commandLine += `-${option} `;
      if (value !== true) {
        commandLine += `${value} `;
      }
    }
  }
  return commandLine;
}

function isEnabled(val:string) {
  const matchResult = val && typeof val === 'string' && val.match("[Yy1]");
  return matchResult  && matchResult !== null  && matchResult.length > 0;
}

enum NVSType {
  NVS_TYPE_U8 = 0x01,
  NVS_TYPE_I8 = 0x11,
  NVS_TYPE_U16 = 0x02,
  NVS_TYPE_I16 = 0x12,
  NVS_TYPE_U32 = 0x04,
  NVS_TYPE_I32 = 0x14,
  NVS_TYPE_U64 = 0x08,
  NVS_TYPE_I64 = 0x18,
  NVS_TYPE_STR = 0x21,
  NVS_TYPE_BLOB = 0x42,
  NVS_TYPE_ANY = 0xff
};

interface ConfigEntry {
  value: number | string;
  type?: NVSType;
}

interface Config {
  [key: string]: ConfigEntry | string;
}
interface BtIcon {
  label: string;
  icon: string;
}

interface BatIcon {
  icon: string;
  label: string;
  ranges: Array<{ f: number; t: number }>;
}

interface BtStateIcon {
  desc: string;
  sub: string[];
}
const btIcons: { [key: string]: BtIcon } = {
  bt_playing: { label: '', icon: 'media_bluetooth_on' },
  bt_disconnected: { label: '', icon: 'media_bluetooth_off' },
  bt_neutral: { label: '', icon: 'bluetooth' },
  bt_connecting: { label: '', icon: 'bluetooth_searching' },
  bt_connected: { label: '', icon: 'bluetooth_connected' },
  bt_disabled: { label: '', icon: 'bluetooth_disabled' },
  play_arrow: { label: '', icon: 'play_circle_filled' },
  pause: { label: '', icon: 'pause_circle' },
  stop: { label: '', icon: 'stop_circle' },
  '': { label: '', icon: '' }
};

const batIcons: BatIcon[] = [
  { icon: "battery_0_bar", label: '▪', ranges: [{ f: 5.8, t: 6.8 }, { f: 8.8, t: 10.2 }] },
  { icon: "battery_2_bar", label: '▪▪', ranges: [{ f: 6.8, t: 7.4 }, { f: 10.2, t: 11.1 }] },
  { icon: "battery_3_bar", label: '▪▪▪', ranges: [{ f: 7.4, t: 7.5 }, { f: 11.1, t: 11.25 }] },
  { icon: "battery_4_bar", label: '▪▪▪▪', ranges: [{ f: 7.5, t: 7.8 }, { f: 11.25, t: 11.7 }] }
];

const btStateIcons: BtStateIcon[] = [
  { desc: 'Idle', sub: ['bt_neutral'] },
  { desc: 'Discovering', sub: ['bt_connecting'] },
  { desc: 'Discovered', sub: ['bt_connecting'] },
  { desc: 'Unconnected', sub: ['bt_disconnected'] },
  { desc: 'Connecting', sub: ['bt_connecting'] },
  {
    desc: 'Connected',
    sub: ['bt_connected', 'play_arrow', 'bt_playing', 'pause', 'stop'],
  },
  { desc: 'Disconnecting', sub: ['bt_disconnected'] },
];

const connectReturnCode = {
  OK: 0,
  FAIL: 1,
  DISC: 2,
  LOST: 3,
  RESTORE: 4,
  ETH: 5
}
const taskStates = [
  'eRunning',
  /*! < A task is querying the state of itself, so must be running. */
  'eReady',
  /*! < The task being queried is in a read or pending ready list. */
  'eBlocked',
  /*! < The task being queried is in the Blocked state. */
  'eSuspended',
  /*! < The task being queried is in the Suspended state, or is in the Blocked state with an infinite time out. */
  'eDeleted'
];
let flashState = {
  NONE: 0,
  REBOOT_TO_RECOVERY: 2,
  SET_FWURL: 5,
  FLASHING: 6,
  DONE: 7,
  UPLOADING: 8,
  ERROR: 9,
  UPLOADCOMPLETE: 10,
  _state: -1,
  olderRecovery: false,
  statusText: '',
  flashURL: '',
  flashFileName: '',
  statusPercent: 0,
  Completed: false,
  recovery: false,
  prevRecovery: false,
  updateModal: new bootstrap.Modal(document.getElementById('otadiv'), {}),
  reset: function () {

    this.olderRecovery = false;
    this.statusText = '';
    this.statusPercent = -1;
    this.flashURL = '';
    this.flashFileName = undefined;
    this.UpdateProgress();
    $('#rTable tr.release').removeClass('table-success table-warning');
    $('.flact').prop('disabled', false);
    ($('#flashfilename')[0] as HTMLInputElement).value = null;
    ($('#fw-url-input')[0] as HTMLInputElement).value = null;

    if (!this.isStateError()) {
      $('span#flash-status').html('');
      $('#fwProgressLabel').parent().removeClass('bg-danger');
    }
    this._state = this.NONE
    return this;
  },
  isStateUploadComplete: function () {
    return this._state == this.UPLOADCOMPLETE;
  },
  isStateError: function () {
    return this._state == this.ERROR;
  },
  isStateNone: function () {
    return this._state == this.NONE;
  },
  isStateRebootRecovery: function () {
    return this._state == this.REBOOT_TO_RECOVERY;
  },
  isStateSetUrl: function () {
    return this._state == this.SET_FWURL;
  },
  isStateFlashing: function () {
    return this._state == this.FLASHING;
  },
  isStateDone: function () {
    return this._state == this.DONE;
  },
  isStateUploading: function () {
    return this._state == this.UPLOADING;
  },
  init: function () {
    this._state = this.NONE;
    return this;
  },

  SetStateError: function () {
    this._state = this.ERROR;
    $('#fwProgressLabel').parent().addClass('bg-danger');
    return this;
  },
  SetStateNone: function () {
    this._state = this.NONE;
    return this;
  },
  SetStateRebootRecovery: function () {
    this._state = this.REBOOT_TO_RECOVERY;
    // Reboot system to recovery mode
    this.SetStatusText('Starting recovery mode.')
    $.ajax({
      url: '/recovery.json',
      context: this,
      dataType: 'text',
      method: 'POST',
      cache: false,
      contentType: 'application/json; charset=utf-8',
      data: JSON.stringify({
        timestamp: Date.now(),
      }),
      error: function (xhr, _ajaxOptions, thrownError) {
        this.setOTAError(`Unexpected error while trying to restart to recovery. (status=${xhr.status ?? ''}, error=${thrownError ?? ''} ) `);
      },
      complete: function (response) {
        this.SetStatusText('Waiting for system to boot.')
      },
    });
    return this;
  },
  SetStateSetUrl: function () {
    this._state = this.SET_FWURL;
    this.statusText = 'Sending firmware download location.';
    let confData = {
      fwurl: {
        value: this.flashURL,
        type: 33,
      }
    };
    post_config(confData);
    return this;
  },
  SetStateFlashing: function () {
    this._state = this.FLASHING;
    return this;
  },
  SetStateDone: function () {
    this._state = this.DONE;
    this.reset();
    return this;
  },
  SetStateUploading: function () {
    this._state = this.UPLOADING;
    return this.SetStatusText('Sending file to device.');
  },
  SetStateUploadComplete: function () {
    this._state = this.UPLOADCOMPLETE;
    return this;
  },

  isFlashExecuting: function () {
    return true === (this._state != this.UPLOADING && (this.statusText !== '' || this.statusPercent >= 0));
  },



  toString: function () {
    let keys = Object.keys(this);
    return keys.find(x => this[x] === this._state);
  },

  setOTATargets: function () {
    this.flashURL = '';
    this.flashFileName = '';
    this.flashURL = $('#fw-url-input').val();
    let fileInputctrl = $('#flashfilename')[0] as HTMLInputElement;
    let fileInput = fileInputctrl.files;
    if (fileInput.length > 0) {
      this.flashFileName = fileInput[0];
    }
    if (this.flashFileName.length == 0 && this.flashURL.length == 0) {
      this.setOTAError('Invalid url or file. Cannot start OTA');
    }
    return this;
  },

  setOTAError: function (message: string) {
    this.SetStateError().SetStatusPercent(0).SetStatusText(message).reset();
    return this;
  },

  ShowDialog: function () {
    if (!this.isStateNone()) {
      this.updateModal.show();
      $('.flact').prop('disabled', true);
    }
    return this;
  },

  SetStatusPercent: function (pct: number) {
    var pctChanged = (this.statusPercent != pct);
    this.statusPercent = pct;
    if (pctChanged) {
      if (!this.isStateUploading() && !this.isStateFlashing()) {
        this.SetStateFlashing();
      }
      if (pct == 100) {
        if (this.isStateFlashing()) {
          this.SetStateDone();
        }
        else if (this.isStateUploading()) {
          this.statusPercent = 0;
          this.SetStateFlashing();
        }
      }
      this.UpdateProgress().ShowDialog();
    }
    return this;
  },
  SetStatusText: function (txt: string) {
    var changed = (this.statusText != txt);
    this.statusText = txt;
    if (changed) {
      $('span#flash-status').html(this.statusText);
      this.ShowDialog();
    }

    return this;
  },
  UpdateProgress: function () {
    $('.progress-bar')
      .css('width', this.statusPercent + '%')
      .attr('aria-valuenow', this.statusPercent)
      .text(this.statusPercent + '%')
    $('.progress-bar').html((this.isStateDone() ? 100 : this.statusPercent) + '%');
    return this;
  },
  StartOTA: function () {
    this.logEvent(this.StartOTA.name);
    $('#fwProgressLabel').parent().removeClass('bg-danger');
    this.setOTATargets();
    if (this.isStateError()) {
      return this;
    }
    if (!recovery) {
      this.SetStateRebootRecovery();
    }
    else {
      this.SetStateFlashing().TargetReadyStartOTA();
    }

    return this;
  },
  UploadLocalFile: function () {
    this.SetStateUploading();
    const xhttp = new XMLHttpRequest();
    var boundHandleUploadProgressEvent = this.HandleUploadProgressEvent.bind(this);
    var boundsetOTAError = this.setOTAError.bind(this);
    xhttp.upload.addEventListener("progress", boundHandleUploadProgressEvent, false);
    xhttp.onreadystatechange = function () {
      if (xhttp.readyState === 4) {
        if (xhttp.status === 0 || xhttp.status === 404) {
          boundsetOTAError(`Upload Failed. Recovery version might not support uploading. Please use web update instead.`);
        }
      }
    };
    xhttp.open('POST', '/flash.json', true);
    xhttp.send(this.flashFileName);
  },
  TargetReadyStartOTA: function () {
    if (recovery && this.prevRecovery && !this.isStateRebootRecovery() && !this.isStateFlashing()) {
      // this should only execute once, while being in a valid state
      return this;
    }

    this.logEvent(this.TargetReadyStartOTA.name);
    if (!recovery) {
      console.error('Event TargetReadyStartOTA fired in the wrong mode ');
      return this;
    }
    this.prevRecovery = true;

    if (this.flashFileName !== '') {
      this.UploadLocalFile();
    }
    else if (this.flashURL != '') {
      this.SetStateSetUrl();
    }
    else {
      this.setOTAError('Invalid URL or file name while trying to start the OTa process')
    }
  },

  HandleUploadProgressEvent: function (data: StatusObject) {
    this.logEvent(this.HandleUploadProgressEvent.name);
    this.SetStateUploading().SetStatusPercent(Math.round(data.loaded / data.total * 100)).SetStatusText('Uploading file to device');
  },
  EventTargetStatus: function (data: StatusObject) {
    if (!this.isStateNone()) {
      this.logEvent(this.EventTargetStatus.name);
    }
    if (data.ota_pct ?? -1 >= 0) {
      this.olderRecovery = true;
      this.SetStatusPercent(data.ota_pct);
    }
    if ((data.ota_dsc ?? '') != '') {
      this.olderRecovery = true;
      this.SetStatusText(data.ota_dsc);
    }

    if (data.recovery != undefined) {
      this.recovery = data.recovery === 1 ? true : false;
    }
    if (this.isStateRebootRecovery() && this.recovery) {
      this.TargetReadyStartOTA();
    }
  },
  EventOTAMessageClass: function (data: string) {
    this.logEvent(this.EventOTAMessageClass.name);
    var otaData: StatusObject = JSON.parse(data);
    this.SetStatusPercent(otaData.ota_pct).SetStatusText(otaData.ota_dsc);
  },
  logEvent: function (fun: string) {
    console.log(`${fun}, flash state ${this.toString()}, recovery: ${this.recovery}, ota pct: ${this.statusPercent}, ota desc: ${this.statusText}`);
  }

};


let presetsloaded = false;
let is_i2c_locked = false;
let statusInterval = 2000;
let messageInterval = 2500;
function post_config(data: object) {
  let confPayload = {
    timestamp: Date.now(),
    config: data
  };
  $.ajax({
    url: '/config.json',
    dataType: 'text',
    method: 'POST',
    cache: false,
    contentType: 'application/json; charset=utf-8',
    data: JSON.stringify(confPayload),
    error: handleExceptionResponse,
  });
}

type CommandValuesEntry = Record<string, string | boolean>;
type CommandValues = Record<string, CommandValuesEntry>;
interface ParsedCommand {
  name: string;
  output: string;
  options: Record<string, string>;
  otherValues: string;
  otherOptions: { btname: string | null; n: string | null };
}

function parseSqueezeliteCommandLine(commandLine: string): ParsedCommand {
  const options: Record<string, string> = {};
  let output: string, name: string;
  let otherValues = '';

  const argRegex = /("[^"]+"|'[^']+'|\S+)/g;
  const args = commandLine.match(argRegex) || [];

  let i = 0;

  while (i < args.length) {
    const arg = args[i];

    if (arg.startsWith('-')) {
      const option = arg.slice(1);

      if (option === '') {
        otherValues += args.slice(i).join(' ');
        break;
      }

      let value = "";

      if (i + 1 < args.length && !args[i + 1].startsWith('-')) {
        value = args[i + 1].replace(/"/g, '').replace(/'/g, '');
        i++;
      }

      options[option] = value;
    } else {
      otherValues += arg + ' ';
    }

    i++;
  }

  otherValues = otherValues.trim();
  output = getOutput(options);
  name = getName(options);

  let otherOptions: { btname: string | null; n: string | null } = { btname: null, n: null };

  // Assign 'o' and 'n' options to otherOptions if present
  if (options.o && output.toUpperCase() === 'BT') {
    let temp = parseSqueezeliteCommandLine(options.o);
    if (temp.name) {
      otherOptions.btname = temp.name;
    }
    delete options.o;
  }
  if (options.n) {
    otherOptions.n = options.n;
    delete options.n;
  }
  return { name, output, options, otherValues, otherOptions };
}


function getOutput(options: Record<string, string>) {
  let output;
  if (options.o) {
    output = options.o.replace(/"/g, '').replace(/'/g, '');
    /* set output as the first alphanumerical word in the command line */
    if (output.indexOf(' ') > 0) {
      output = output.substring(0, output.indexOf(' '));
    }
  }
  return output;
}

function getName(options: Record<string, string>) {
  let name;
  /* if n option present, assign to name variable */
  if (options.n) {
    name = options.n.replace(/"/g, '').replace(/'/g, '');
  }
  return name;
}


function isConnected(): boolean {
  return ConnectedTo != undefined && ConnectedTo.hasOwnProperty('ip') && ConnectedTo.ip != '0.0.0.0' && ConnectedTo.ip != '';
}
function getIcon(icons: RssiIcon) {
  return isConnected() ? icons.icon : icons.label;
}
function handlebtstate(data: StatusObject) {
  let icon: BtIcon = { label: '', icon: '' };
  let tt = '';

  if (data.bt_status !== undefined && data.bt_sub_status !== undefined) {
    const iconIndex = btStateIcons[data.bt_status]?.sub[data.bt_sub_status];
    if (iconIndex) {
      icon = btIcons[iconIndex];
      tt = btStateIcons[data.bt_status].desc;
    } else {
      icon = btIcons.bt_connected;
      tt = 'Output status';
    }
  }

  $('#o_type').attr('title', tt);
  $('#o_bt').html(isConnected() ? icon.label : icon.icon); // Note: Assuming `isConnected()` is defined elsewhere.
}

function handleTemplateTypeRadio(outtype: OutputType): void {
  $('#o_type').children('span').css({ display: 'none' });
  let changed = false;

  if (outtype !== output) {
    changed = true;
    output = outtype;
  }

  $('#' + output).prop('checked', true);
  $('#o_' + output).css({ display: 'inline' });

  if (changed) {
    Object.entries(commandDefaults[output]).forEach(([key, value]) => {
      $(`#cmd_opt_${key}`).val(value);
    });
  }
}

function handleExceptionResponse(xhr: JQuery.jqXHR<any>
  , _ajaxOptions: JQuery.Ajax.ErrorTextStatus, thrownError: string) {
  console.log(xhr.status);
  console.log(thrownError);
  if (thrownError !== '') {
    showLocalMessage(thrownError, 'MESSAGING_ERROR');
  }
}
function HideCmdMessage(cmdname: string) {
  $('#toast_' + cmdname)
    .removeClass('table-success')
    .removeClass('table-warning')
    .removeClass('table-danger')
    .addClass('table-success')
    .removeClass('show');
  $('#msg_' + cmdname).html('');
}
function showCmdMessage(cmdname: string, msgtype: string, msgtext: string, append = false) {
  let color = 'table-success';
  if (msgtype === 'MESSAGING_WARNING') {
    color = 'table-warning';
  } else if (msgtype === 'MESSAGING_ERROR') {
    color = 'table-danger';
  }
  $('#toast_' + cmdname)
    .removeClass('table-success')
    .removeClass('table-warning')
    .removeClass('table-danger')
    .addClass(color)
    .addClass('show');
  let escapedtext = msgtext
    .substring(0, msgtext.length - 1)
    .encodeHTML()
    .replace(/\n/g, '<br />');
  escapedtext =
    ($('#msg_' + cmdname).html().length > 0 && append
      ? $('#msg_' + cmdname).html() + '<br/>'
      : '') + escapedtext;
  $('#msg_' + cmdname).html(escapedtext);
}

let releaseURL =
  'https://api.github.com/repos/sle118/squeezelite-esp32/releases';

let recovery = false;
let messagesHeld = false;
let commandBTSinkName = '';
const commandHeader = 'squeezelite ';
interface CommandOptions {
  b: string;
  C: string;
  W: string;
  Z: string;
  o: string;
}

const commandDefaults: { [key: string]: CommandOptions } = {
  i2s: { b: "500:2000", C: "30", W: "", Z: "96000", o: "I2S" },
  spdif: { b: "500:2000", C: "30", W: "", Z: "48000", o: "SPDIF" },
  bt: { b: "500:2000", C: "30", W: "", Z: "44100", o: "BT" },
};

let validOptions = {
  codecs: ['flac', 'pcm', 'mp3', 'ogg', 'aac', 'wma', 'alac', 'dsd', 'mad', 'mpg']
};

//let blockFlashButton = false;
let apList = null;
//let selectedSSID = '';
//let checkStatusInterval = null;
let messagecount = 0;
let messageseverity:string = 'MESSAGING_INFO';
let SystemConfig: any;
let LastCommandsState: number = NaN;
var output = '';
let hostName = '';
let versionName = 'Squeezelite-ESP32';
let prevmessage = '';
let project_name = versionName;
let depth = 16;
let board_model = '';
let platform_name = versionName;
let preset_name = '';
let btSinkNamesOptSel = '#cfg-audio-bt_source-sink_name';
let ConnectedTo: NetworkConnection;
let ConnectingToSSID: NetworkConnection;
let lmsBaseUrl: string = "";
let prevLMSIP = '';
const ConnectingToActions = {
  'CONN': 0, 'MAN': 1, 'STS': 2,
}

function delay<T>(promise: Promise<T>, duration: number): Promise<T> {
  return new Promise((resolve, reject) => {
    promise.then(
      value => setTimeout(() => resolve(value), duration),
      reason => setTimeout(() => reject(reason), duration)
    );
  });
}

function getConfigJson(slimMode: boolean): Config {
  const config: Config = {};
  $('input.nvs').each(function (_index, element) {
    const entry = element as HTMLInputElement;
    const nvsTypeAttr = entry.attributes.getNamedItem('nvs_type');
    if (!slimMode && nvsTypeAttr) {
      const nvsType = parseInt(nvsTypeAttr.value, 10) as NVSType;
      if (entry.id !== '') {
        const value = (nvsType <= NVSType.NVS_TYPE_I64) ? parseInt(entry.value, 10) : entry.value;
        config[entry.id] = {
          value: value,
          type: nvsType,
        };
      }
    } else {
      if (entry.id !== '') {
        config[entry.id] = entry.value;
      }
    }
  });

  // In the following, we assume that `#nvs-new-key` and `#nvs-new-value`
  // correspond to input elements and thus their values are always strings.
  const key = ($('#nvs-new-key') as JQuery<HTMLInputElement>).val();
  const val = ($('#nvs-new-value') as JQuery<HTMLInputElement>).val();

  if (key && key !== '') {
    if (!slimMode) {
      config[key] = {
        value: val,
        type: NVSType.NVS_TYPE_I8, // Assuming a default type here
      };
    } else {
      config[key] = val;
    }
  }
  return config;
}



function handleHWPreset(allfields:  NodeListOf<HTMLInputElement>, reboot: boolean): void {
  const selJson = JSON.parse(allfields[0].value);
  const cmd = allfields[0].getAttribute("cmdname");
  console.log(`selected model: ${selJson.name}`);
  let confPayload: ConfigPayload = {
    timestamp: Date.now(),
    config: { model_config: { value: selJson.name, type: 33 } } // Assuming 33 is some sort of default type
  };

  for (const [name, value] of Object.entries(selJson.config)) {
    const storedval = (typeof value === 'string' || value instanceof String) ? value : JSON.stringify(value);
    confPayload.config[name] = {
      value: storedval.toString(),
      type: NVSType.NVS_TYPE_STR,
    };
    showCmdMessage(
      cmd,
      'MESSAGING_INFO',
      `Setting ${name}=${storedval} `,
      true
    );
  }

  showCmdMessage(
    cmd,
    'MESSAGING_INFO',
    `Committing `,
    true
  );

  $.ajax({
    url: '/config.json',
    dataType: 'text',
    method: 'POST',
    cache: false,
    contentType: 'application/json; charset=utf-8',
    data: JSON.stringify(confPayload),
    error: function (xhr, _ajaxOptions, thrownError) {
      handleExceptionResponse(xhr, _ajaxOptions, thrownError);
      showCmdMessage(
        cmd,
        'MESSAGING_ERROR',
        `Unexpected error ${(thrownError !== '') ? thrownError : 'with return status = ' + xhr.status} `,
        true
      );
    },
    success: function (response) {
      showCmdMessage(
        cmd,
        'MESSAGING_INFO',
        `Saving complete `,
        true
      );
      console.log(response);
      if (reboot) {
        delayReboot(2500, cmd);
      }
    },
  });
}

// pull json file from https://gist.githubusercontent.com/sle118/dae585e157b733a639c12dc70f0910c5/raw/b462691f69e2ad31ac95c547af6ec97afb0f53db/squeezelite-esp32-presets.json and
function loadPresets() {
  if ($("#cfg-hw-preset-model_config").length == 0) return;
  if (presetsloaded) return;
  presetsloaded = true;
  $('#cfg-hw-preset-model_config').html('<option>--</option>');
  $.getJSON(
    'https://gist.githubusercontent.com/sle118/dae585e157b733a639c12dc70f0910c5/raw/',
    { _: new Date().getTime() },
    function (data) {
      $.each(data, function (key, val) {
        $('#cfg-hw-preset-model_config').append(`<option value='${JSON.stringify(val).replace(/"/g, '\"').replace(/\'/g, '\"')}'>${val.name}</option>`);
        if (preset_name !== '' && preset_name == val.name) {
          $('#cfg-hw-preset-model_config').val(preset_name);
        }
      });
      if (preset_name !== '') {
        $('#prev_preset').show().val(preset_name);
      }
    }

  ).fail(function (jqxhr, textStatus, error) {
    const err = textStatus + ', ' + error;
    console.log('Request Failed: ' + err);
  }
  );
}

function delayReboot(duration: number, cmdname: string, ota = 'reboot'): void {
  const url = `/${ota}.json`;
  $('tbody#tasks').empty();
  $('#tasks_sect').css('visibility', 'collapse');

  delay(Promise.resolve({ cmdname: cmdname, url: url }), duration)
    .then(function (data) {
      // Your existing logic here
      console.log('now triggering reboot');
      $("button[onclick*='handleReboot']").addClass('rebooting');
      $.ajax({
        // Your existing AJAX call setup here
        complete: function () {
          console.log('reboot call completed');
          delay(Promise.resolve(data), 6000)
            .then(function (rdata) {
              // Your existing logic here
            });
        },
      });
    });
}

function saveAutoexec1(apply: boolean) {
  showCmdMessage('cfg-audio-tmpl', 'MESSAGING_INFO', 'Saving.\n', false);
  let commandLine = `${commandHeader} -o ${output} `;
  $('.sqcmd').each(function () {
    let { opt, val } = get_control_option_value($(this));
    if ((opt && opt.length > 0) && typeof (val) == 'boolean' || typeof (val) === 'string' && val.length > 0) {
      const optStr = opt === ':' ? opt : (` -${opt} `);
      val = typeof (val) == 'boolean' ? '' : val;
      commandLine += `${optStr} ${val}`;
    }
  });
  const resample = $('#cmd_opt_R input[name=resample]:checked');
  if (resample.length > 0 && resample.attr('suffix') !== '') {
    commandLine += resample.attr('suffix');
    // now check resample_i option and if checked, add suffix to command line
    if ($('#resample_i').is(":checked") && resample.attr('aint') == 'true') {
      commandLine += $('#resample_i').attr('suffix');
    }
  }


  if (output === 'bt') {
    showCmdMessage(
      'cfg-audio-tmpl',
      'MESSAGING_INFO',
      'Remember to configure the Bluetooth audio device name.\n',
      true
    );
  }
  // commandLine += concatenateOptions(options);
  const data = {
    timestamp: Date.now(),
    config: {
      autoexec1: { value: commandLine, type: NVSType.NVS_TYPE_STR }
    }
  };

  $.ajax({
    url: '/config.json',
    dataType: 'text',
    method: 'POST',
    cache: false,
    contentType: 'application/json; charset=utf-8',
    data: JSON.stringify(data),
    error: handleExceptionResponse,
    complete: function (response) {
      if (
        response.responseText &&
        JSON.parse(response.responseText).result === 'OK'
      ) {
        showCmdMessage('cfg-audio-tmpl', 'MESSAGING_INFO', 'Done.\n', true);
        if (apply) {
          delayReboot(1500, 'cfg-audio-tmpl');
        }
      } else if (JSON.parse(response.responseText).result) {
        showCmdMessage(
          'cfg-audio-tmpl',
          'MESSAGING_WARNING',
          JSON.parse(response.responseText).Result + '\n',
          true
        );
      } else {
        showCmdMessage(
          'cfg-audio-tmpl',
          'MESSAGING_ERROR',
          response.statusText + '\n'
        );
      }
      console.log(response.responseText);
    },
  });
  console.log('sent data:', JSON.stringify(data));
}
function handleDisconnect() {
  $.ajax({
    url: '/connect.json',
    dataType: 'text',
    method: 'DELETE',
    cache: false,
    contentType: 'application/json; charset=utf-8',
    data: JSON.stringify({
      timestamp: Date.now(),
    }),
  });
}
function setPlatformFilter(val: string) {
  if ($('.upf').filter(function () { return $(this).text().toUpperCase() === val.toUpperCase() }).length > 0) {
    $('#splf').val(val).trigger('input');
    return true;
  }
  return false;
}
function handleConnect() {
  ConnectingToSSID.ssid = $('#manual_ssid').val().toString();
  ConnectingToSSID.pwd = $('#manual_pwd').val().toString();
  ConnectingToSSID.dhcpname = $('#dhcp-name2').val().toString();
  $("*[class*='connecting']").hide();
  $('#ssid-wait').text(ConnectingToSSID.ssid);
  $('.connecting').show();
  $.ajax({
    url: '/connect.json',
    dataType: 'text',
    method: 'POST',
    cache: false,
    contentType: 'application/json; charset=utf-8',
    data: JSON.stringify({
      timestamp: Date.now(),
      ssid: ConnectingToSSID.ssid,
      pwd: ConnectingToSSID.pwd
    }),
    error: handleExceptionResponse,
  });

  // now we can re-set the intervals regardless of result

}
function renderError(opt: string, error: string) {
  const fieldname = `cmd_opt_${opt}`;
  let errorFieldName = `${fieldname}-error`;
  let errorField = $(`#${errorFieldName}`);
  let field = $(`#${fieldname}`);

  if (!errorField || errorField.length == 0) {
    field.after(`<div id="${errorFieldName}" class="invalid-feedback"></div>`);
    errorField = $(`#${errorFieldName}`);
  }
  if (error.length == 0) {
    errorField.hide();
    field.removeClass('is-invalid');
    field.addClass('is-valid');
    errorField.text('');
  }
  else {
    errorField.show();
    errorField.text(error);
    field.removeClass('is-valid');
    field.addClass('is-invalid');
  }
  return errorField;
}
$(function () {
  $('.material-icons').each(function (_index, entry) {
    entry.setAttribute('data-icon', entry.textContent || '');
  });
  setIcons(true);
  handleNVSVisible();
  flashState.init();
  $('#fw-url-input').on('input', function () {
    const stringVal = $(this).val().toString();
    if (stringVal.length > 8 && (stringVal.startsWith('http://') || stringVal.startsWith('https://'))) {
      $('#start-flash').show();
    }
    else {
      $('#start-flash').hide();
    }
  });
  $('.upSrch').on('input', function () {
    const inputField: HTMLInputElement = this as HTMLInputElement;
    const val = inputField.value;
    $("#rTable tr").removeClass(inputField.id + '_hide');
    if (val.length > 0) {
      $(`#rTable td:nth-child(${$(inputField).parent().index() + 1})`).filter(function () {
        return !$(this).text().toUpperCase().includes(val.toUpperCase());
      }).parent().addClass(this.id + '_hide');
    }
    $('[class*="_hide"]').hide();
    $('#rTable tr').not('[class*="_hide"]').show()

  });
  setTimeout(refreshAP, 1500);
  /* add validation for cmd_opt_c, which accepts a comma separated list. 
    getting known codecs from validOptions.codecs array
    use bootstrap classes to highlight the error with an overlay message */
  $('#options input').on('input', function () {
    const inputField: HTMLInputElement = this as HTMLInputElement;
    const { opt, val } = get_control_option_value(this);
    if (opt === 'c' || opt === 'e') {
      const fieldname = `cmd_opt_${opt}_codec-error`;

      const values = val.toString().split(',').map(function (item) {
        return item.trim();
      });
      /* get a list of invalid codecs */
      const invalid = values.filter(function (item) {
        return !validOptions.codecs.includes(item);
      });
      renderError(opt, invalid.length > 0 ? `Invalid codec(s) ${invalid.join(', ')}` : '');
    }
    /* add validation for cmd_opt_m, which accepts a mac_address */
    if (opt === 'm') {
      const mac_regex = /^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$/;
      renderError(opt, mac_regex.test(val.toString()) ? '' : 'Invalid MAC address');
    }
    if (opt === 'r') {
      const rateRegex = /^(\d+\.?\d*|\.\d+)-(\d+\.?\d*|\.\d+)$|^(\d+\.?\d*)$|^(\d+\.?\d*,)+\d+\.?\d*$/;
      renderError(opt, rateRegex.test(val.toString()) ? '' : `Invalid rate(s) ${val}. Acceptable format: <maxrate>|<minrate>-<maxrate>|<rate1>,<rate2>,<rate3>`);
    }



  }


  );





  $('#WifiConnectDialog')[0].addEventListener('shown.bs.modal', function (event: any) {
    $("*[class*='connecting']").hide();

    if (event?.relatedTarget) {
      ConnectingToSSID.Action = ConnectingToActions.CONN;
      if ($(event.relatedTarget).children('td:eq(1)').text() == ConnectedTo.ssid) {
        ConnectingToSSID.Action = ConnectingToActions.STS;
      }
      else {
        if (!$(event.relatedTarget).is(':last-child')) {
          ConnectingToSSID.ssid = $(event.relatedTarget).children('td:eq(1)').text();
          $('#manual_ssid').val(ConnectingToSSID.ssid);
        }
        else {
          ConnectingToSSID.Action = ConnectingToActions.MAN;
          ConnectingToSSID.ssid = '';
          $('#manual_ssid').val(ConnectingToSSID.ssid);
        }
      }
    }


    if (ConnectingToSSID.Action !== ConnectingToActions.STS) {
      $('.connecting-init').show();
      $('#manual_ssid').trigger('focus');
    }
    else {
      handleWifiDialog();
    }
  });

  $('#WifiConnectDialog')[0].addEventListener('hidden.bs.modal', function () {
    $('#WifiConnectDialog input').val('');
  });

  $('#uCnfrm')[0].addEventListener('shown.bs.modal', function () {
    $('#selectedFWURL').text($('#fw-url-input').val().toString());
  });

  ($('input#show-commands')[0] as HTMLInputElement).checked = LastCommandsState === 1;
  $('a[href^="#tab-commands"]').hide();
  $('#load-nvs').on('click', function () {
    $('#nvsfilename').trigger('click');
  });
  $('#nvsfilename').on('change', function () {
    const _this = this as HTMLInputElement;
    if (typeof window.FileReader !== 'function') {
      throw "The file API isn't supported on this browser.";
    }
    if (!_this.files) {
      throw 'This browser does not support the `files` property of the file input.';
    }
    if (!_this.files[0]) {
      return undefined;
    }

    const file = _this.files[0];
    let fr = new FileReader();
    fr.onload = function (e) {
      let data: Record<string, string>;
      try {
        data = JSON.parse(e.target.result.toString());
      } catch (ex) {
        alert('Parsing failed!\r\n ' + ex);
      }
      $('input.nvs').each(function (_index, entry: HTMLInputElement) {
        $(this).parent().removeClass('bg-warning').removeClass('bg-success');
        if (data[entry.id]) {
          if (data[entry.id] !== entry.value) {
            console.log(
              'Changed ' + entry.id + ' ' + entry.value + '==>' + data[entry.id]
            );
            $(this).parent().addClass('bg-warning');
            $(this).val(data[entry.id]);
          }
          else {
            $(this).parent().addClass('bg-success');
          }
        }
      });
      var changed = $("input.nvs").children('.bg-warning');
      if (changed) {
        alert('Highlighted values were changed. Press Commit to change on the device');
      }
    }
    fr.readAsText(file);
    _this.value = null;

  }
  );
  $('#clear-syslog').on('click', function () {
    messagecount = 0;
    messageseverity = 'MESSAGING_INFO';
    $('#msgcnt').text('');
    $('#syslogTable').html('');
  });

  $('#ok-credits').on('click', function () {
    $('#credits').slideUp('fast', function () { });
    $('#app').slideDown('fast', function () { });
  });

  $('#acredits').on('click', function (event) {
    event.preventDefault();
    $('#app').slideUp('fast', function () { });
    $('#credits').slideDown('fast', function () { });
  });

  $('input#show-commands').on('click', function () {
    const _this = this as HTMLInputElement;
    _this.checked = _this.checked ? true : false;
    if (_this.checked) {
      $('a[href^="#tab-commands"]').show();
      LastCommandsState = 1;
    } else {
      LastCommandsState = 0;
      $('a[href^="#tab-commands"]').hide();
    }
  });

  $('#disable-squeezelite').on('click', function () {
    // this.checked = this.checked ? 1 : 0;
    // $('#disable-squeezelite').prop('checked')
    const _this = this as HTMLInputElement;
    if (_this.checked) {
      // Store the current value before overwriting it
      const currentValue = $('#cmd_opt_s').val();
      $('#cmd_opt_s').data('originalValue', currentValue);

      // Overwrite the value with '-disable'
      $('#cmd_opt_s').val('-disable');
    } else {
      // Retrieve the original value
      const originalValue = $('#cmd_opt_s').data('originalValue');

      // Restore the original value if it exists, otherwise set it to an empty string
      $('#cmd_opt_s').val(originalValue ? originalValue : '');
    }

  });



  $('input#show-nvs').on('click', function () {
    const _this = this as HTMLInputElement;
    _this.checked = _this.checked ? true : false;
    Cookies.set("show-nvs", _this.checked ? 'Y' : 'N');
    handleNVSVisible();
  });
  $('#btn_reboot_recovery').on('click', function () {
    handleReboot('recovery');
  });
  $('#btn_reboot').on('click', function () {
    handleReboot('reboot');
  });
  $('#btn_flash').on('click', function () {
    hFlash();
  });
  $('#save-autoexec1').on('click', function () {
    saveAutoexec1(false);
  });
  $('#commit-autoexec1').on('click', function () {
    saveAutoexec1(true);
  });
  $('#btn_disconnect').on('click', function () {
    ConnectedTo = resetNetworkConnection();
    refreshAPHTML2();
    $.ajax({
      url: '/connect.json',
      dataType: 'text',
      method: 'DELETE',
      cache: false,
      contentType: 'application/json; charset=utf-8',
      data: JSON.stringify({
        timestamp: Date.now(),
      }),
    });
  });
  $('#btnJoin').on('click', function () {
    handleConnect();
  });
  $('#reboot_nav').on('click', function () {
    handleReboot('reboot');
  });
  $('#reboot_ota_nav').on('click', function () {
    handleReboot('reboot_ota');
  });

  $('#save-as-nvs').on('click', function () {
    const config = getConfigJson(true);
    const a = document.createElement('a');
    a.href = URL.createObjectURL(
      new Blob([JSON.stringify(config, null, 2)], {
        type: 'text/plain',
      })
    );
    a.setAttribute(
      'download',
      'nvs_config_' + hostName + '_' + Date.now() + 'json'
    );
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
  });

  $('#save-nvs').on('click', function () {
    post_config(getConfigJson(false));
  });

  $('#fwUpload').on('click', function () {
    const fileInput = (document.getElementById('flashfilename') as HTMLInputElement).files;
    if (fileInput.length === 0) {
      alert('No file selected!');
    } else {
      ($('#fw-url-input') as unknown as HTMLInputElement).value = null;
      flashState.StartOTA();
    }

  });
  $('[name=output-tmpl]').on('click', function () {
    const outputType = this.id as OutputType;
    handleTemplateTypeRadio(outputType);
  });

  $('#chkUpdates').on('click', function () {
    $('#rTable').html('');
    $.getJSON(releaseURL, function (data) {
      let i = 0;
      const branches: string[] = [];
      data.forEach(function (release: ReleaseEntry) {
        const namecomponents = release.name.split('#');
        const branch = namecomponents[3];
        if (!branches.includes(branch)) {
          branches.push(branch);
        }
      });
      let fwb = '';
      branches.forEach(function (branch) {
        fwb += '<option value="' + branch + '">' + branch + '</option>';
      });
      $('#fwbranch').append(fwb);

      data.forEach(function (release: ReleaseEntry) {
        let url = '';
        release.assets.forEach(function (asset) {
          if (asset.name.match(/\.bin$/)) {
            url = asset.browser_download_url;
          }
        });
        const namecomponents = release.name.split('#');
        const ver = namecomponents[0];
        const cfg = namecomponents[2];
        const branch = namecomponents[3];
        var bits = ver.substr(ver.lastIndexOf('-') + 1);
        bits = (bits == '32' || bits == '16') ? bits : '';

        let body = release.body;
        body = body.replace(/'/gi, '"');
        body = body.replace(
          /[\s\S]+(### Revision Log[\s\S]+)### ESP-IDF Version Used[\s\S]+/,
          '$1'
        );
        body = body.replace(/- \(.+?\) /g, '- ').encodeHTML();
        $('#rTable').append(`<tr class='release ' fwurl='${url}'>
        <td data-bs-toggle='tooltip' title='${body}'>${ver}</td><td>${new Date(release.created_at).toLocalShort()}
        </td><td class='upf'>${cfg}</td><td>${branch}</td><td>${bits}</td></tr>`
        );
      });
      if (i > 7) {
        $('#releaseTable').append(
          "<tr id='showall'>" +
          "<td colspan='6'>" +
          "<input type='button' id='showallbutton' class='btn btn-info' value='Show older releases' />" +
          '</td>' +
          '</tr>'
        );
        $('#showallbutton').on('click', function () {
          $('tr.hide').removeClass('hide');
          $('tr#showall').addClass('hide');
        });
      }
      $('#searchfw').css('display', 'inline');
      if (!setPlatformFilter(platform_name)) {
        setPlatformFilter(project_name)
      }
      $('#rTable tr.release').on('click', function () {
        var url = this.getAttribute('fwurl');
        if (lmsBaseUrl) {
          url = url.replace(/.*\/download\//, lmsBaseUrl + '/plugins/SqueezeESP32/firmware/');
        }
        $('#fw-url-input').val(url);
        $('#start-flash').show();
        $('#rTable tr.release').removeClass('table-success table-warning');
        $(this).addClass('table-success table-warning');
      });

    }).fail(function () {
      alert('failed to fetch release history!');
    });
  });
  $('#fwcheck').on('click', function () {
    $('#releaseTable').html('');
    $('#fwbranch').empty();
    $.getJSON(releaseURL, function (data) {
      let i = 0;
      const branches: string[] = [];
      data.forEach(function (release: ReleaseEntry) {
        const namecomponents = release.name.split('#');
        const branch = namecomponents[3];
        if (!branches.includes(branch)) {
          branches.push(branch);
        }
      });
      let fwb: string;
      branches.forEach(function (branch) {
        fwb += `<option value="${branch}">${branch}</option>`;
      });
      $('#fwbranch').append(fwb);

      data.forEach(function (release: ReleaseEntry) {
        let url = '';
        release.assets.forEach(function (asset) {
          if (asset.name.match(/\.bin$/)) {
            url = asset.browser_download_url;
          }
        });
        const namecomponents = release.name.split('#');
        const ver = namecomponents[0];
        const idf = namecomponents[1];
        const cfg = namecomponents[2];
        const branch = namecomponents[3];

        let body = release.body;
        body = body.replace(/'/gi, '"');
        body = body.replace(
          /[\s\S]+(### Revision Log[\s\S]+)### ESP-IDF Version Used[\s\S]+/,
          '$1'
        );
        body = body.replace(/- \(.+?\) /g, '- ');
        const trclass = i++ > 6 ? ' hide' : '';
        $('#releaseTable').append(
          `<tr class='release${trclass}'><td data-bs-toggle='tooltip' title='${body}'>${ver}</td><td>${new Date(release.created_at).toLocalShort()}</td><td>${cfg}</td><td>${idf}</td><td>${branch}</td><td><input type='button' class='btn btn-success' value='Select' data-bs-url='${url}' onclick='setURL(this);' /></td></tr>`
        );
      });
      if (i > 7) {
        $('#releaseTable').append(
          `<tr id='showall'><td colspan='6'><input type='button' id='showallbutton' class='btn btn-info' value='Show older releases' /></td></tr>`
        );
        $('#showallbutton').on('click', function () {
          $('tr.hide').removeClass('hide');
          $('tr#showall').addClass('hide');
        });
      }
      $('#searchfw').css('display', 'inline');
    }).fail(function () {
      alert('failed to fetch release history!');
    });
  });

  $('#updateAP').on('click', function () {
    refreshAP();
    console.log('refresh AP');
  });

  // first time the page loads: attempt to get the connection status and start the wifi scan
  getConfig();
  getCommands();
  getMessages();
  checkStatus();

});

// eslint-disable-next-line no-unused-vars
window.setURL = function (button: HTMLButtonElement) {
  let url = button.dataset.url;

  $('[data-bs-url^="http"]')
    .addClass('btn-success')
    .removeClass('btn-danger');
  $('[data-bs-url="' + url + '"]')
    .addClass('btn-danger')
    .removeClass('btn-success');

  // if user can proxy download through LMS, modify the URL
  if (lmsBaseUrl) {
    url = url.replace(/.*\/download\//, lmsBaseUrl + '/plugins/SqueezeESP32/firmware/');
  }

  $('#fwurl').val(url);
}

interface RssiIcon {
  label: string;
  icon: string;
}

function rssiToIcon(rssi: number): RssiIcon {
  if (rssi >= -55) {
    return { label: '****', icon: `signal_wifi_statusbar_4_bar` };
  } else if (rssi >= -60) {
    return { label: '***', icon: `network_wifi_3_bar` };
  } else if (rssi >= -65) {
    return { label: '**', icon: `network_wifi_2_bar` };
  } else if (rssi >= -70) {
    return { label: '*', icon: `network_wifi_1_bar` };
  } else {
    return { label: '.', icon: `signal_wifi_statusbar_null` };
  }
}


function refreshAP() {
  if (ConnectedTo?.urc === connectReturnCode.ETH) return;
  $.ajaxSetup({
    timeout: 3000 //Time in milliseconds
  });
  $.getJSON('/scan.json', async function () {
    await sleep(2000);
    $.getJSON('/ap.json', function (data: NetworkConnection[]) {
      if (data.length > 0) {
        // sort by signal strength
        data.sort(function (a, b) {
          const x = a.rssi;
          const y = b.rssi;
          // eslint-disable-next-line no-nested-ternary
          return x < y ? 1 : x > y ? -1 : 0;
        });
        apList = data;
        refreshAPHTML2(apList);

      }
    });
  });
}
function formatAP(ssid: string, rssi: number, auth: number) {
  const rssi_icon: RssiIcon = rssiToIcon(rssi);
  const auth_icon = { label: auth == 0 ? '🔓' : '🔒', icon: auth == 0 ? 'no_encryption' : 'lock' };

  return `<tr data-bs-toggle="modal" data-bs-target="#WifiConnectDialog"><td></td><td>${ssid}</td><td>
  <span class="material-icons" style="fill:white; display: inline" aria-label="${rssi_icon.label}" icon="${rssi_icon.icon}" >${getIcon(rssi_icon)}</span>
  	</td><td>
    <span class="material-icons" aria-label="${auth_icon.label}" icon="${auth_icon.icon}">${getIcon(auth_icon)}</span>
  </td></tr>`;
}
function refreshAPHTML2(data?: NetworkConnection[]) {
  let h = '';
  $('#wifiTable tr td:first-of-type').text('');
  $('#wifiTable tr').removeClass('table-success table-warning');
  if (data) {
    data.forEach(function (e: NetworkConnection) {
      h += formatAP(e.ssid, e.rssi, e.auth);
    });
    $('#wifiTable').html(h);
  }
  if ($('.manual_add').length == 0) {
    $('#wifiTable').append(formatAP('Manual add', 0, 0));
    $('#wifiTable tr:last').addClass('table-light text-dark').addClass('manual_add');
  }
  if (ConnectedTo && ConnectedTo.ssid && (ConnectedTo.urc === connectReturnCode.OK || ConnectedTo.urc === connectReturnCode.RESTORE)) {
    const wifiSelector = `#wifiTable td:contains("${ConnectedTo.ssid}")`;
    if ($(wifiSelector).filter(function () { return $(this).text() === ConnectedTo.ssid; }).length == 0) {
      $('#wifiTable').prepend(`${formatAP(ConnectedTo.ssid, ConnectedTo.rssi ?? 0, 0)}`);
    }
    $(wifiSelector).filter(function () { return $(this).text() === ConnectedTo.ssid; }).siblings().first().html('&check;').parent().addClass((ConnectedTo.urc === connectReturnCode.OK ? 'table-success' : 'table-warning'));
    $('span#foot-if').html(`SSID: <strong>${ConnectedTo.ssid}</strong>, IP: <strong>${ConnectedTo.ip}</strong>`);
    const rssiIconObj = rssiToIcon(ConnectedTo.rssi); // Assume this returns an object like { label: 'some_label', icon: 'some_icon_name' }
    const iconTextContent = getIcon(rssiIconObj); // Function to get the text content for the material icon

    // Set the icon text content
    $('#wifiStsIcon').text(iconTextContent);

    // Update the aria-label and custom icon attribute
    $('#wifiStsIcon').attr('aria-label', rssiIconObj.label);
    $('#wifiStsIcon').attr('icon', rssiIconObj.icon);
  }
  else if (ConnectedTo?.urc !== connectReturnCode.ETH) {
    $('span#foot-if').html('');
  }

}
function refreshETH() {

  if (ConnectedTo.urc === connectReturnCode.ETH) {
    $('span#foot-if').html(`Network: Ethernet, IP: <strong>${ConnectedTo.ip}</strong>`);
  }
}
function showTask(task: TaskDetails) {
  console.debug(
    `${this.toLocaleString()}\t${task.nme}\t${task.cpu}\t${taskStates[task.st]}\t${task.minstk}\t${task.bprio}\t${task.cprio}\t${task.num}`
  );
  $('tbody#tasks').append(
    `<tr class="table-primary"><th scope="row">${task.num}</th><td>${task.nme}</td><td>${task.cpu}</td><td>${taskStates[task.st]}</td><td>${task.minstk}</td><td>${task.bprio}</td><td>${task.cprio}</td></tr>`
  );
}
function btExists(name: string) {
  return getBTSinkOpt(name).length > 0;
}
function getBTSinkOpt(name: string) {
  return $(`${btSinkNamesOptSel} option:contains('${name}')`);
}
function getMessages() {
  $.ajaxSetup({
    timeout: messageInterval //Time in milliseconds
  });
  $.getJSON('/messages.json', async function (data) {
    for (const msg of data) {
      const msgAge = msg.current_time - msg.sent_time;
      var msgTime = new Date();
      msgTime.setTime(msgTime.getTime() - msgAge);
      switch (msg.class) {
        case 'MESSAGING_CLASS_OTA':
          flashState.EventOTAMessageClass(msg.message);
          break;
        case 'MESSAGING_CLASS_STATS':
          // for task states, check structure : task_state_t
          var statsData = JSON.parse(msg.message);
          console.debug(
            msgTime.toLocalShort() +
            ' - Number of running tasks: ' +
            statsData.ntasks
          );
          console.debug(
            `${msgTime.toLocalShort()}\tname\tcpu\tstate\tminstk\tbprio\tcprio\tnum`
          );
          if (statsData.tasks) {
            const taskList = statsData.tasks as TaskDetails[]
            if ($('#tasks_sect').css('visibility') === 'collapse') {
              $('#tasks_sect').css('visibility', 'visible');
            }
            $('tbody#tasks').html('');
            statsData.taskList
              .sort(function (a: TaskDetails, b: TaskDetails) {
                return b.cpu - a.cpu;
              })
              .forEach(showTask, msgTime);
          } else if ($('#tasks_sect').css('visibility') === 'visible') {
            $('tbody#tasks').empty();
            $('#tasks_sect').css('visibility', 'collapse');
          }
          break;
        case 'MESSAGING_CLASS_SYSTEM':
          showMessage(msg, msgTime);
          break;
        case 'MESSAGING_CLASS_CFGCMD':
          var msgparts = msg.message.split(/([^\n]*)\n(.*)/gs);
          showCmdMessage(msgparts[1], msg.type, msgparts[2], true);
          break;
        case 'MESSAGING_CLASS_BT':
          if ($(btSinkNamesOptSel).is('input')) {
            const sinkNameCtrl = $(btSinkNamesOptSel)[0] as HTMLInputElement;
            var attr = sinkNameCtrl.attributes;
            var attrs = '';
            for (var j = 0; j < attr.length; j++) {
              if (attr.item(j).name != "type") {
                attrs += `${attr.item(j).name} = "${attr.item(j).value}" `;
              }
            }
            var curOpt = sinkNameCtrl.value;
            $(btSinkNamesOptSel).replaceWith(`<select id="cfg-audio-bt_source-sink_name" ${attrs}><option value="${curOpt}" data-bs-description="${curOpt}">${curOpt}</option></select> `);
          }

          JSON.parse(msg.message).forEach(function (btEntry: BTDevice) {
            // [{\n\t\t\"name\":\t\"SMSL BT4.2\",\n\t\t\"rssi\":\t-64\n\t}]
            //<input type="text" class="form-control bg-success" placeholder="name" hasvalue="true" longopts="sink_name" shortopts="n" checkbox="false" cmdname="cfg-audio-bt_source" id="cfg-audio-bt_source-sink_name" name="cfg-audio-bt_source-sink_name">
            //<select hasvalue="true" longopts="jack_behavior" shortopts="j" checkbox="false" cmdname="cfg-audio-general" id="cfg-audio-general-jack_behavior" name="cfg-audio-general-jack_behavior" class="form-control "><option>--</option><option>Headphones</option><option>Subwoofer</option></select>            
            if (!btExists(btEntry.name)) {
              $(btSinkNamesOptSel).append(`<option>${btEntry.name}</option>`);
              showMessage({
                type: msg.type, 
                message: `BT Audio device found: ${btEntry.name} RSSI: ${btEntry.rssi} `,
                class: '',
                sent_time: 0,
                current_time: 0
              }, msgTime);
            }
            getBTSinkOpt(btEntry.name).attr('data-bs-description', `${btEntry.name} (${btEntry.rssi}dB)`)
              .attr('rssi', btEntry.rssi)
              .attr('value', btEntry.name)
              .text(`${btEntry.name} [${btEntry.rssi}dB]`).trigger('change');

          });
          // Get the options as an array
          const btEntries = Array.from($(btSinkNamesOptSel).find('option'));

          // Sort the options based on the 'rssi' attribute
          btEntries.sort(function (a, b) {
            const rssiA = parseInt($(a).attr('rssi'), 10);
            const rssiB = parseInt($(b).attr('rssi'), 10);
            console.log(`${rssiA} < ${rssiB} ? `);
            return rssiB - rssiA; // Sort by descending RSSI values
          });

          // Clear the select element and append the sorted options
          $(btSinkNamesOptSel).empty().append(btEntries);
          break;
        default:
          break;
      }
    }
    setTimeout(getMessages, messageInterval);
  }).fail(function (xhr, ajaxOptions, thrownError) {

    if (xhr.status == 404) {
      $('.orec').hide(); // system commands won't be available either
      messagesHeld = true;
    }
    else {
      handleExceptionResponse(xhr, ajaxOptions, thrownError);
    }
    if (xhr.status == 0 && xhr.readyState == 0) {
      // probably a timeout. Target is rebooting? 
      setTimeout(getMessages, messageInterval * 2); // increase duration if a failure happens
    }
    else if (!messagesHeld) {
      // 404 here means we rebooted to an old recovery
      setTimeout(getMessages, messageInterval); // increase duration if a failure happens
    }

  }
  );

  /*
    Minstk is minimum stack space left
Bprio is base priority
cprio is current priority
nme is name
st is task state. I provided a "typedef" that you can use to convert to text
cpu is cpu percent used
*/
}
function handleRecoveryMode(data: StatusObject) {
  const locRecovery = data.recovery ?? 0;
  if (locRecovery === 1) {
    recovery = true;
    $('.recovery_element').show();
    $('.ota_element').hide();
    $('#boot-button').html('Reboot');
    $('#boot-form').attr('action', '/reboot_ota.json');
  } else {
    if (!recovery && messagesHeld) {
      messagesHeld = false;
      setTimeout(getMessages, messageInterval); // increase duration if a failure happens
    }
    recovery = false;

    $('.recovery_element').hide();
    $('.ota_element').show();
    $('#boot-button').html('Recovery');
    $('#boot-form').attr('action', '/recovery.json');
  }

}

function hasConnectionChanged(data: StatusObject) {
  // gw: "192.168.10.1"
  // ip: "192.168.10.225"
  // netmask: "255.255.255.0"
  // ssid: "MyTestSSID"

  return (ConnectedTo && (data.urc !== ConnectedTo.urc ||
    data.ssid !== ConnectedTo.ssid ||
    data.gw !== ConnectedTo.gw ||
    data.netmask !== ConnectedTo.netmask ||
    data.ip !== ConnectedTo.ip || data.rssi !== ConnectedTo.rssi))
}
function handleWifiDialog(data?: StatusObject) {
  if ($('#WifiConnectDialog').is(':visible')) {
    if (ConnectedTo.ip) {
      $('#ipAddress').text(ConnectedTo.ip);
    }
    if (ConnectedTo.ssid) {
      $('#connectedToSSID').text(ConnectedTo.ssid);
    }
    if (ConnectedTo.gw) {
      $('#gateway').text(ConnectedTo.gw);
    }
    if (ConnectedTo.netmask) {
      $('#netmask').text(ConnectedTo.netmask);
    }
    if (ConnectingToSSID.Action === undefined || (ConnectingToSSID.Action && ConnectingToSSID.Action == ConnectingToActions.STS)) {
      $("*[class*='connecting']").hide();
      $('.connecting-status').show();
    }
    if (SystemConfig.ap_ssid) {
      $('#apName').text(SystemConfig.ap_ssid.value);
    }
    if (SystemConfig.ap_pwd) {
      $('#apPass').text(SystemConfig.ap_pwd.value);
    }
    if (!data) {
      return;
    }
    else {
      switch (data.urc) {
        case connectReturnCode.OK:
          if (data.ssid && data.ssid === ConnectingToSSID.ssid) {
            $("*[class*='connecting']").hide();
            $('.connecting-success').show();
            ConnectingToSSID.Action = ConnectingToActions.STS;
          }
          break;
        case connectReturnCode.FAIL:
          // 
          if (ConnectingToSSID.Action != ConnectingToActions.STS && ConnectingToSSID.ssid == data.ssid) {
            $("*[class*='connecting']").hide();
            $('.connecting-fail').show();
          }
          break;
        case connectReturnCode.LOST:

          break;
        case connectReturnCode.RESTORE:
          if (ConnectingToSSID.Action != ConnectingToActions.STS && ConnectingToSSID.ssid != data.ssid) {
            $("*[class*='connecting']").hide();
            $('.connecting-fail').show();
          }
          break;
        case connectReturnCode.DISC:
          // that's a manual disconnect
          // if ($('#wifi-status').is(':visible')) {
          //   $('#wifi-status').slideUp('fast', function() {});
          //   $('span#foot-wifi').html('');

          // }                 
          break;
        default:
          break;
      }
    }

  }
}
function setIcons(offline: boolean): void {
  $('.material-icons').each(function (_index, entry: Element) {
    const htmlEntry = entry as HTMLElement;
    htmlEntry.textContent = htmlEntry.getAttribute(offline ? 'aria-label' : 'data-icon') || '';
  });
}
function assignStatusToNetworkConnection(data: StatusObject): NetworkConnection {
  const connection: NetworkConnection = {
    urc: data.urc ?? 0, // Assuming `urc` should default to 0 if undefined in data
    auth: undefined, // This doesn't exist in StatusObject, so it remains undefined
    pwd: undefined, // Also doesn't exist in StatusObject
    dhcpname: undefined, // Also doesn't exist in StatusObject
    Action: undefined, // Also doesn't exist in StatusObject
    ip: data.ip,
    ssid: data.ssid,
    rssi: data.rssi,
    gw: data.gw,
    netmask: data.netmask
  };
  return connection;
}
function handleNetworkStatus(data: StatusObject) {
  setIcons(!isConnected());
  if (hasConnectionChanged(data) || !data.urc) {
    ConnectedTo = assignStatusToNetworkConnection(data);
    $(".if_eth").hide();
    $('.if_wifi').hide();
    if (!data.urc || ConnectedTo.urc != connectReturnCode.ETH) {
      $('.if_wifi').show();
      refreshAPHTML2();
    }
    else {
      $(".if_eth").show();
      refreshETH();
    }

  }
  handleWifiDialog(data);
}



function batteryToIcon(voltage: number) {
  /* Assuming Li-ion 18650s as a power source, 3.9V per cell, or above is treated
  as full charge (>75% of capacity).  3.4V is empty. The gauge is loosely
  following the graph here:
    https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages
  using the 0.2C discharge profile for the rest of the values.
*/

  for (const iconEntry of batIcons) {
    for (const entryRanges of iconEntry.ranges) {
      if (inRange(voltage, entryRanges.f, entryRanges.t)) {
        return { label: iconEntry.label, icon: iconEntry.icon };
      }
    }
  }


  return { label: '▪▪▪▪', icon: "battery_full" };
}
function checkStatus() {
  $.ajaxSetup({
    timeout: statusInterval //Time in milliseconds
  });
  $.getJSON('/status.json', function (data) {
    handleRecoveryMode(data);
    handleNVSVisible();
    handleNetworkStatus(data);
    handlebtstate(data);
    flashState.EventTargetStatus(data);
    if (data.depth) {
      depth = data.depth;
      if (depth == 16) {
        $('#cmd_opt_R').show();
      }
      else {
        $('#cmd_opt_R').hide();
      }
    }


    if (data.project_name && data.project_name !== '') {
      project_name = data.project_name;
    }
    if (data.platform_name && data.platform_name !== '') {
      platform_name = data.platform_name;
    }
    if (board_model === '') board_model = project_name;
    if (board_model === '') board_model = 'Squeezelite-ESP32';
    if (data.version && data.version !== '') {
      versionName = data.version;
      $("#navtitle").html(`${board_model}${recovery ? '<br>[recovery]' : ''}`);
      $('span#foot-fw').html(`fw: <strong>${versionName}</strong>, mode: <strong>${recovery ? "Recovery" : project_name}</strong>`);
    } else {
      $('span#flash-status').html('');
    }
    if (data.Voltage) {
      const bat_icon = batteryToIcon(data.Voltage);
      $('#battery').html(`${getIcon(bat_icon)}`);
      $('#battery').attr("aria-label", bat_icon.label);
      $('#battery').attr("data-icon", bat_icon.icon);
      $('#battery').show();
    } else {
      $('#battery').hide();
    }
    if ((data.message ?? '') != '' && prevmessage != data.message) {
      // supporting older recovery firmwares - messages will come from the status.json structure
      prevmessage = data.message;
      showLocalMessage(data.message, 'MESSAGING_INFO')
    }
    is_i2c_locked = data.is_i2c_locked;
    if (is_i2c_locked) {
      $('flds-cfg-hw-preset').hide();
    }
    else {
      $('flds-cfg-hw-preset').show();
    }
    $("button[onclick*='handleReboot']").removeClass('rebooting');

    if (typeof lmsBaseUrl == "undefined" || data.lms_ip != prevLMSIP && data.lms_ip && data.lms_port) {
      const baseUrl = 'http://' + data.lms_ip + ':' + data.lms_port;
      prevLMSIP = data.lms_ip;
      $.ajax({
        url: baseUrl + '/plugins/SqueezeESP32/firmware/-check.bin',
        type: 'HEAD',
        dataType: 'text',
        cache: false,
        error: function () {
          // define the value, so we don't check it any more.
          lmsBaseUrl = '';
        },
        success: function () {
          lmsBaseUrl = baseUrl;
        }
      });
    }
    $('#o_jack').css({ display: Number(data.Jack) ? 'inline' : 'none' });
    setTimeout(checkStatus, statusInterval);
  }).fail(function (xhr, ajaxOptions, thrownError) {
    handleExceptionResponse(xhr, ajaxOptions, thrownError);
    if (xhr.status == 0 && xhr.readyState == 0) {
      // probably a timeout. Target is rebooting? 
      setTimeout(checkStatus, messageInterval * 2); // increase duration if a failure happens
    }
    else {
      setTimeout(checkStatus, messageInterval); // increase duration if a failure happens
    }
  });
}
// eslint-disable-next-line no-unused-vars
window.runCommand = function (button, reboot) {
  let cmdstring = button.getAttribute('cmdname');
  showCmdMessage(
    cmdstring,
    'MESSAGING_INFO',
    'Executing.',
    false
  );
  const fields = document.getElementById('flds-' + cmdstring);
  const allfields = fields?.querySelectorAll('select,input') as NodeListOf<HTMLInputElement>;
  if (cmdstring === 'cfg-hw-preset') return handleHWPreset(allfields, reboot);
  cmdstring += ' ';
  if (fields) {
    for (const field of allfields) {
      let qts = '';
      let opt = '';
      const isSelect = field.tagName === 'SELECT';
      const hasValue = field.getAttribute('hasvalue') === 'true';
      const validVal = (isSelect && field.value !== '--') || (!isSelect && field.value !== '');

      if (!hasValue || (hasValue && validVal)) {
        const longopts = field.getAttribute('longopts');
        const shortopts = field.getAttribute('shortopts');

        if (longopts !== null && longopts !== 'undefined') {
          opt += '--' + longopts;
        } else if (shortopts !== null && shortopts !== 'undefined') {
          opt = '-' + shortopts;
        }

        if (hasValue) {
          qts = /\s/.test(field.value) ? '"' : '';
          cmdstring += `${opt} ${qts}${field.value}${qts} `;
        } else {
          // this is a checkbox
          if (field.checked) {
            cmdstring += `${opt} `;
          }
        }
      }
    }
  }

  console.log(cmdstring);

  const data = {
    timestamp: Date.now(),
    command: cmdstring
  };
  

  $.ajax({
    url: '/commands.json',
    dataType: 'text',
    method: 'POST',
    cache: false,
    contentType: 'application/json; charset=utf-8',
    data: JSON.stringify(data),
    error: function (xhr, _ajaxOptions, thrownError) {
      var cmd = JSON.parse(this.data).command;
      if (xhr.status == 404) {
        showCmdMessage(
          cmd.substr(0, cmd.indexOf(' ')),
          'MESSAGING_ERROR',
          `${recovery ? 'Limited recovery mode active. Unsupported action ' : 'Unexpected error while processing command'}`,
          true
        );
      }
      else {
        handleExceptionResponse(xhr, _ajaxOptions, thrownError);
        showCmdMessage(
          cmd.substr(0, cmd.indexOf(' ') - 1),
          'MESSAGING_ERROR',
          `Unexpected error ${(thrownError !== '') ? thrownError : 'with return status = ' + xhr.status}`,
          true
        );
      }
    },
    success: function (response) {
      $('.orec').show();
      console.log(response);
      if (
        JSON.parse(response).Result === 'Success' &&
        reboot
      ) {
        delayReboot(2500, button.getAttribute('cmdname'));
      }
    },
  });
}
function getLongOps(data:CommandValues, name:string, longopts:string) {
  return data[name] !== undefined ? data[name][longopts] : "";
}
function getCommands() {
  $.ajaxSetup({
    timeout: 7000 //Time in milliseconds
  });
  $.getJSON('/commands.json', function (data) {
    console.log(data);
    $('.orec').show();
    data.commands.forEach(function (command:CommandEntry) {
      if ($('#flds-' + command.name).length === 0) {
        const cmdParts = command.name.split('-');
        const isConfig = cmdParts[0] === 'cfg';
        const targetDiv = '#tab-' + cmdParts[0] + '-' + cmdParts[1];
        let innerhtml = '';
        innerhtml += `<div class="card text-white mb-3"><div class="card-header">${command.help.encodeHTML().replace(/\n/g, '<br />')}</div><div class="card-body"><fieldset id="flds-${command.name}">`;
        if (command.argtable) {
          command.argtable.forEach(function (arg) {
            let placeholder = arg.datatype || '';
            const ctrlname = command.name + '-' + arg.longopts;
            const curvalue = getLongOps(data.values, command.name, arg.longopts);

            let attributes = `hasvalue=${arg.hasvalue} `;
            attributes += 'longopts="' + arg.longopts + '" ';
            attributes += 'shortopts="' + arg.shortopts + '" ';
            attributes += 'checkbox=' + arg.checkbox + ' ';
            attributes += 'cmdname="' + command.name + '" ';
            attributes +=
              `id="${ctrlname}" name="${ctrlname}" hasvalue="${arg.hasvalue}"   `;
            let extraclass = arg.mincount > 0 ? 'bg-success' : '';
            if (arg.glossary === 'hidden') {
              attributes += ' style="visibility: hidden;"';
            }
            if (arg.checkbox) {
              innerhtml += `<div class="form-check"><label class="form-check-label"><input type="checkbox" ${attributes} class="form-check-input ${extraclass}" value="" >${arg.glossary.encodeHTML()}</label>`;
            } else {
              innerhtml += `<div class="form-group" ><label for="${ctrlname}">${arg.glossary.encodeHTML()}</label>`;
              if (placeholder.includes('|')) {
                extraclass = placeholder.startsWith('+') ? ' multiple ' : '';
                placeholder = placeholder
                  .replace('<', '')
                  .replace('=', '')
                  .replace('>', '');
                innerhtml += `<select ${attributes} class="form-control ${extraclass}" >`;
                placeholder = '--|' + placeholder;
                placeholder.split('|').forEach(function (choice) {
                  innerhtml += '<option >' + choice + '</option>';
                });
                innerhtml += '</select>';
              } else {
                innerhtml += `<input type="text" class="form-control ${extraclass}" placeholder="${placeholder}" ${attributes}>`;
              }
            }

            innerhtml += `${arg.checkbox ? '</div>' : ''}<small class="form-text text-muted">Previous value: ${arg.checkbox ? (curvalue ? 'Checked' : 'Unchecked') : (curvalue || '')}</small>${arg.checkbox ? '' : '</div>'}`;
          });
        }
        innerhtml += `<div style="margin-top: 16px;">
        <div class="toast hide" role="alert" aria-live="assertive" aria-atomic="true" id="toast_${command.name}">
        <div class="toast-header">
        <strong class="mr-auto">Result</strong
          <button type="button" class="btn-close" data-bs-dismiss="toast" aria-label="Close"></button>
        </div>
        <div class="toast-body" id="msg_${command.name}"></div>
      </div>`;
        if (isConfig) {
          innerhtml +=
            `<button type="submit" class="btn btn-info sclk" id="btn-save-${command.name}" cmdname="${command.name}">Save</button>
<button type="submit" class="btn btn-warning cclk" id="btn-commit-${command.name}" cmdname="${command.name}">Apply</button>`;
        } else {
          innerhtml += `<button type="submit" class="btn btn-success sclk" id="btn-run-${command.name}" cmdname="${command.name}">Execute</button>`;
        }
        innerhtml += '</div></fieldset></div></div>';
        if (isConfig) {
          $(targetDiv).append(innerhtml);
        } else {
          $('#commands-list').append(innerhtml);
        }
      }
    });
    $(".sclk").off('click').on('click', function () { window.runCommand((this as HTMLButtonElement), false); });
    $(".cclk").off('click').on('click', function () { window.runCommand((this as HTMLButtonElement), true); });
    data.commands.forEach(function (command:CommandEntry) {
      $('[cmdname=' + command.name + ']:input').val('');
      $('[cmdname=' + command.name + ']:checkbox').prop('checked', false);
      if (command.argtable) {
        command.argtable.forEach(function (arg) {
          const ctrlselector = '#' + command.name + '-' + arg.longopts;
          if (arg.checkbox) {
            ($(ctrlselector)[0] as HTMLInputElement).checked = getLongOps(data, command.name, arg.longopts) as boolean;
          } else {
            let ctrlValue = getLongOps(data, command.name, arg.longopts);
            if (ctrlValue !== undefined) {
              $(ctrlselector)
                .val(ctrlValue.toString())
                .trigger('change');
            }
            if (
            ($(ctrlselector)[0] as HTMLInputElement).value.length === 0 &&
              (arg.datatype || '').includes('|')
            ) {
              ($(ctrlselector)[0] as HTMLInputElement).value = '--';
            }
          }
        });
      }
    });
    loadPresets();
  }).fail(function (xhr, ajaxOptions, thrownError) {
    if (xhr.status == 404) {
      $('.orec').hide();
    }
    else {
      handleExceptionResponse(xhr, ajaxOptions, thrownError);
    }
    $('#commands-list').empty();

  });
}

function getConfig() {
  $.ajaxSetup({
    timeout: 7000 //Time in milliseconds
  });
  $.getJSON('/config.json', function (entries) {
    $('#nvsTable tr').remove();
    const data = (entries.config ? entries.config : entries);
    SystemConfig = data;
    commandBTSinkName = '';
    Object.keys(data)
      .sort()
      .forEach(function (key) {
        let val = data[key].value;
        if (key === 'autoexec1') {
          /* call new function to parse the squeezelite options */
          processSqueezeliteCommandLine(val);
        } else if (key === 'host_name') {
          val = val.replaceAll('"', '');
          $('input#dhcp-name1').val(val);
          $('input#dhcp-name2').val(val);
          if ($('#cmd_opt_n').length == 0) {
            $('#cmd_opt_n').val(val);
          }
          document.title = val;
          hostName = val;
        } else if (key === 'rel_api') {
          releaseURL = val;
        }
        else if (key === 'enable_airplay') {
          $("#s_airplay").css({ display: isEnabled(val) ? 'inline' : 'none' })
        }
        else if (key === 'enable_cspot') {
          $("#s_cspot").css({ display: isEnabled(val) ? 'inline' : 'none' })
        }
        else if (key == 'preset_name') {
          preset_name = val;
        }
        else if (key == 'board_model') {
          board_model = val;
        }

        $('tbody#nvsTable').append(
          `<tr><td>${key}</td><td class='value'><input type='text' class='form-control nvs' id='${key}'  nvs_type=${data[key].type} ></td></tr>`
        );
        $('input#' + key).val(data[key].value);
      });
    if (commandBTSinkName.length > 0) {
      // persist the sink name found in the autoexec1 command line
      $('#cfg-audio-bt_source-sink_name').val(commandBTSinkName);
    }
    $('tbody#nvsTable').append(
      "<tr><td><input type='text' class='form-control' id='nvs-new-key' placeholder='new key'></td><td><input type='text' class='form-control' id='nvs-new-value' placeholder='new value' nvs_type=33 ></td></tr>"
    );
    if (entries.gpio) {
      $('#pins').show();
      $('tbody#gpiotable tr').remove();


      entries.gpio.forEach(function (gpioEntry: GPIOEntry) {
        $('tbody#gpiotable').append(
          `<tr class=${gpioEntry.fixed ? 'table-secondary' : 'table-primary'}><th scope="row">${gpioEntry.group}</th><td>${gpioEntry.name}</td><td>${gpioEntry.gpio}</td><td>${gpioEntry.fixed ? 'Fixed' : 'Configuration'}</td></tr>`
        );
      });
    }
    else {
      $('#pins').hide();
    }
  }).fail(function (xhr, ajaxOptions, thrownError) {
    handleExceptionResponse(xhr, ajaxOptions, thrownError);
  });
}

function processSqueezeliteCommandLine(val:string) {
  const parsed = parseSqueezeliteCommandLine(val);
  if (parsed.output.toUpperCase().startsWith('I2S')) {
    handleTemplateTypeRadio('i2s');
  } else if (parsed.output.toUpperCase().startsWith('SPDIF')) {
    handleTemplateTypeRadio('spdif');
  } else if (parsed.output.toUpperCase().startsWith('BT')) {
    if (parsed.otherOptions.btname) {
      commandBTSinkName = parsed.otherOptions.btname;
    }
    handleTemplateTypeRadio('bt');

  }
  Object.keys(parsed.options).forEach(function (key) {
    const option = parsed.options[key];
    if (!$(`#cmd_opt_${key}`).hasOwnProperty('checked')) {
      $(`#cmd_opt_${key}`).val(option);
    } else {
      if(typeof option === 'boolean'){
        ($(`#cmd_opt_${key}`)[0] as HTMLInputElement).checked = option ;
    }
    }
  });
  if (parsed.options.hasOwnProperty('u')) {
    // parse -u v[:i] and check the appropriate radio button with id #resample_v
    const [resampleValue, resampleInterpolation] = parsed.options.u.split(':');
    $(`#resample_${resampleValue}`).prop('checked', true);
    // if resampleinterpolation is set, check  resample_i checkbox
    if (resampleInterpolation) {
      $('#resample_i').prop('checked', true);
    }
  }
  if (parsed.options.hasOwnProperty('s')) {
    // parse -u v[:i] and check the appropriate radio button with id #resample_v
    if (parsed.options.s === '-disable') {
      ($('#disable-squeezelite')[0] as HTMLInputElement).checked = true;
    }
    else {
      ($('#disable-squeezelite')[0] as HTMLInputElement).checked = false;
    }
  }




}

function showLocalMessage(message:string, severity:string) {
  const msg:MessageEntry = {
    message: message,
    type: severity,
    class: '',
    sent_time: 0,
    current_time: 0
  };
  showMessage(msg, new Date());
}

function showMessage(msg: MessageEntry, msgTime: Date) {
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

function inRange(x:number, min:number, max:number) {
  return (x - min) * (x - max) <= 0;
}

function sleep(ms:number) {
  return new Promise(resolve => setTimeout(resolve, ms));
}
