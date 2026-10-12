// Print qcamera::gCamCapability[0..5] (Samsung cam_capability_t, 0x88a8 bytes) as hex lines "CAP <id> <hex>"
var SZ = 0x88a8;
var p = Process.getModuleByName('camera.sdm660.so').getExportByName('_ZN7qcamera14gCamCapabilityE');
for (var i = 0; i < 6; i++) {
  var c = p.add(i * 4).readPointer();
  if (c.isNull()) { console.log('CAP ' + i + ' null'); continue; }
  var b = new Uint8Array(c.readByteArray(SZ)), s = '';
  for (var j = 0; j < b.length; j++) s += ('0' + b[j].toString(16)).slice(-2);
  console.log('CAP ' + i + ' ' + s);
}
