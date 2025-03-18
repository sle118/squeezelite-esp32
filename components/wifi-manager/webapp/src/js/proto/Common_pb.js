// source: Common.proto
/**
 * @fileoverview
 * @enhanceable
 * @suppress {missingRequire} reports error on implicit type usages.
 * @suppress {messageConventions} JS Compiler reports an error if a variable or
 *     field starts with 'MSG_' and isn't a translatable message.
 * @public
 */
// GENERATED CODE -- DO NOT EDIT!
/* eslint-disable */
// @ts-nocheck

var jspb = require('google-protobuf');
var goog = jspb;
var global =
    (typeof globalThis !== 'undefined' && globalThis) ||
    (typeof window !== 'undefined' && window) ||
    (typeof global !== 'undefined' && global) ||
    (typeof self !== 'undefined' && self) ||
    (function () { return this; }).call(null) ||
    Function('return this')();

var nanopb_pb = require('./nanopb_pb.js');
goog.object.extend(proto, nanopb_pb);
goog.exportSymbol('proto.sys.DeviceTypeEnum', null, global);
goog.exportSymbol('proto.sys.HostEnum', null, global);
goog.exportSymbol('proto.sys.PortEnum', null, global);
/**
 * @enum {number}
 */
proto.sys.DeviceTypeEnum = {
  UNSPECIFIED_TYPE: 0,
  DEVTYPE_SPI: 1,
  DEVTYPE_I2C: 2,
  DEVTYPE__RMII: 3
};

/**
 * @enum {number}
 */
proto.sys.PortEnum = {
  UNSPECIFIED_SYSTPORT: 0,
  SYSTEM: 1,
  DAC_PORT: 2
};

/**
 * @enum {number}
 */
proto.sys.HostEnum = {
  UNSPECIFIED_HOST: 0,
  HOST0: 1,
  HOST1: 2
};

goog.object.extend(exports, proto.sys);
