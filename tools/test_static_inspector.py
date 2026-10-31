#!/usr/bin/env python3
"""Malformed-input and format checks for the read-only bundle parser (no binaries)."""
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from inspect_wizard import MAGIC,TARGETS,Reader,bundle,component_bytes,inspect

def fixture(path_override=None,format_major=6,negative_extent=False):
    # A minimal format-6 bundle with a known payload, not an executable.
    payload=b'known diagnostic metadata'
    data=bytearray(b'\0'*8+MAGIC+payload)
    offset=8+len(MAGIC);header=len(data)
    struct.pack_into('<q',data,0,header)
    data+=struct.pack('<III',format_major,0,len(TARGETS))+b'\x01x'+struct.pack('<QQQQQ',0,0,0,0,0)
    for i,name in enumerate(sorted(TARGETS)):
        name=path_override if i==0 and path_override is not None else name
        encoded=name.encode()
        if len(encoded)>=128:raise ValueError('Fixture path too long')
        data+=struct.pack('<qqqB',-1 if negative_extent else offset,len(payload),0,0)+bytes([len(encoded)])+encoded
    return bytes(data),payload

class BundleTests(unittest.TestCase):
    def test_manifest_and_payload(self):
        data,payload=fixture();manifest=bundle(data)
        self.assertEqual(manifest['format'],'6.0')
        self.assertEqual(manifest['entry_count'],len(TARGETS))
        self.assertEqual(component_bytes(data,manifest['entries'][0]),payload)
    def test_missing_marker(self):
        with self.assertRaises(ValueError):bundle(b'ordinary data')
    def test_truncated_header(self):
        data,_=fixture()
        with self.assertRaises(ValueError):bundle(data[:50])
    def test_unsupported_format(self):
        data,_=fixture(format_major=7)
        with self.assertRaises(ValueError):bundle(data)
    def test_traversal_and_absolute_paths(self):
        for path in ['../outside.dll','/absolute.dll','dir\\escape.dll']:
            with self.subTest(path=path),self.assertRaises(ValueError):bundle(fixture(path_override=path)[0])
    def test_duplicate_entry(self):
        name=sorted(TARGETS)[1]
        with self.assertRaises(ValueError):bundle(fixture(path_override=name)[0])
    def test_missing_required_component(self):
        with self.assertRaises(ValueError):bundle(fixture(path_override='other.dll')[0])
    def test_negative_entry_extent(self):
        with self.assertRaises(ValueError):bundle(fixture(negative_extent=True)[0])
    def test_raw_deflate(self):
        content=b'bounded payload'*100
        c=zlib.compressobj(wbits=-15);raw=c.compress(content)+c.flush()
        entry={'offset':0,'size':len(content),'compressed_size':len(raw)}
        self.assertEqual(component_bytes(raw,entry),content)
    def test_overexpansion_and_truncation(self):
        content=b'A'*4096;c=zlib.compressobj(wbits=-15);raw=c.compress(content)+c.flush()
        with self.assertRaises(ValueError):component_bytes(raw,{'offset':0,'size':10,'compressed_size':len(raw)})
        with self.assertRaises(ValueError):component_bytes(raw[:-2],{'offset':0,'size':len(content),'compressed_size':len(raw)-2})
    def test_invalid_string_length(self):
        with self.assertRaises(ValueError):Reader(b'\xff'*6).string()
    def test_wrong_archive_hash_rejected_before_parse(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'untrusted.zip';path.write_bytes(b'not a ZIP')
            with self.assertRaisesRegex(ValueError,'SHA-256 mismatch'):inspect(path,'0'*64)

if __name__=='__main__':unittest.main()
