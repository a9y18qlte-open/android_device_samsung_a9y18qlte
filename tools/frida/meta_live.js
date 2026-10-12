// Decode a few per-frame fields of Samsung metadata_buffer_t in translateFromHalMetadata (first 6 frames)
var m = Process.getModuleByName('camera.sdm660.so');
var f = m.enumerateExports().filter(function (e) { return e.name.indexOf('translateFromHalMetadata') >= 0; })[0];
var n = 0;
Interceptor.attach(f.address, { onEnter: function (a) {
  if (n++ >= 6) return;
  var p = a[1];
  var v = function (i) { return p.add(i).readU8(); };
  var s = 'META frame#=' + (v(111) ? p.add(378816).readU32() : '-') +
    ' exp_ns=' + (v(145) ? p.add(379232).readS64() : '-') +
    ' iso=' + (v(147) ? p.add(379248).readS32() : '-') +
    ' crop=' + (v(143) ? [0, 4, 8, 12].map(function (o) { return p.add(379212 + o).readS32(); }).join(',') : '-') +
    ' aperture=' + (v(134) ? p.add(379156).readFloat().toFixed(2) : '-') +
    ' stream_info.num=' + p.add(816936 + 64).readU32();
  console.log(s);
}});
