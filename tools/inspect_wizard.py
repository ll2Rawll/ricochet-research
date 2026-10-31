#!/usr/bin/env python3
"""Inspect Activision's public wizard ZIP as data; never execute its contents.

Writes hashes, .NET bundle metadata, PE imports/exports, selected CLR metadata
and call/string references. Requires requirements-static.txt. No network access.
"""
import argparse
import hashlib
import importlib.metadata
import io
import json
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC=bytes.fromhex('8b1202b96a612038727b930214d7a03213f5b9e6efae3318ee3b2dce24b36aae')
ZIP_MEMBER='CODSecureAttestationWizard/CODSecureAttestationWizard.exe'
LIMIT=128*1024*1024
TARGETS={'version.json','CODSecureAttestationWizard.runtimeconfig.json',
         'CODSecureAttestationWizard.dll','SecureAttestation.dll','enrollaik.exe'}
SELECTED_STRINGS={
    'SecureAttestation.dll':{'ActivisionAIK','COD.Broker.Service','PCP_TPM_FW_VERSION',
        'PCP_TPM_MANUFACTURER_ID','AIK_CERT_PRESENT','AIK_KEY_EXISTS','TCG_LOG_PRESENT',
        'TPM2_PRESENT','UEFI_BIOS_MODE','HARDWARE\\DESCRIPTION\\System\\BIOS'},
    'enrollaik.exe':{'ActivisionAIK','Microsoft\\Crypto\\PCPKSP','EnrollAIKWindowClass',
        'certreq.exe -enrollaik -f -q -machine -config "" '},
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

class Reader:
    def __init__(self,data,pos=0):
        self.data=data;self.pos=pos
    def get(self,fmt):
        n=struct.calcsize(fmt)
        if self.pos<0 or self.pos+n>len(self.data):
            raise ValueError('Truncated bundle header')
        result=struct.unpack_from(fmt,self.data,self.pos);self.pos+=n
        return result[0] if len(result)==1 else result
    def string(self):
        size=0
        for shift in range(0,35,7):
            byte=self.get('<B');size|=(byte&127)<<shift
            if not byte&128:break
        else:raise ValueError('Invalid 7-bit string length')
        if size>65536 or self.pos+size>len(self.data):
            raise ValueError('Invalid bundle path/string length')
        result=self.data[self.pos:self.pos+size].decode('utf-8');self.pos+=size
        return result

def bundle(data):
    marker=data.find(MAGIC)
    if marker<8 or data.find(MAGIC,marker+1)>=0:
        raise ValueError('Expected one .NET bundle marker')
    header=struct.unpack_from('<q',data,marker-8)[0]
    r=Reader(data,header)
    major,minor,count=r.get('<III')
    if (major,minor)!=(6,0) or not 1<=count<=5000:
        raise ValueError('Inspector supports bounded .NET bundle format 6.0 only')
    identifier=r.string()
    deps_offset,deps_size,config_offset,config_size,flags=r.get('<QQQQQ')
    entries=[];names=set()
    for _ in range(count):
        offset,size,compressed_size=r.get('<qqq');kind=r.get('<B');name=r.string()
        path=PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or name in names:
            raise ValueError('Unsafe or duplicate bundle path')
        stored_size=compressed_size or size
        if min(offset,size,compressed_size)<0 or size>LIMIT or offset+stored_size>header:
            raise ValueError('Invalid bundle entry extent')
        names.add(name)
        entries.append(dict(path=name,offset=offset,size=size,compressed_size=compressed_size,type=kind))
    missing=TARGETS-names
    if missing:raise ValueError('Missing expected components: '+', '.join(sorted(missing)))
    return dict(format=f'{major}.{minor}',marker_file_offset=marker,
                header_file_offset=header,bundle_id=identifier,entry_count=count,
                flags=flags,deps_json_offset=deps_offset,deps_json_size=deps_size,
                runtimeconfig_offset=config_offset,runtimeconfig_size=config_size,
                entries=entries)

def component_bytes(data,entry):
    size=entry['compressed_size'] or entry['size']
    raw=data[entry['offset']:entry['offset']+size]
    if entry['compressed_size']:
        decompressor=zlib.decompressobj(-15)
        result=decompressor.decompress(raw,entry['size']+1)
        if not decompressor.eof or decompressor.unconsumed_tail or decompressor.unused_data:
            raise ValueError('Invalid or overlarge DEFLATE entry')
    else:result=raw
    if len(result)!=entry['size']:raise ValueError('Uncompressed size mismatch')
    return result

def pe_metadata(data):
    import pefile
    pe=pefile.PE(data=data,fast_load=False)
    imports={i.dll.decode('ascii'):sorted(x.name.decode('ascii') if x.name else '#'+str(x.ordinal)
                                        for x in i.imports)
             for i in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[])}
    exports=[dict(name=x.name.decode('ascii') if x.name else None,rva=x.address,
                  file_offset=pe.get_offset_from_rva(x.address))
             for x in getattr(getattr(pe,'DIRECTORY_ENTRY_EXPORT',None),'symbols',[])]
    versions={}
    for group in getattr(pe,'FileInfo',[]):
        for item in group:
            for table in getattr(item,'StringTable',[]):
                versions.update({k.decode('utf-8','replace'):v.decode('utf-8','replace')
                                 for k,v in table.entries.items()})
    debug=[]
    for entry in getattr(pe,'DIRECTORY_ENTRY_DEBUG',[]):
        if entry.struct.Type==2:
            start=entry.struct.PointerToRawData
            raw=data[start:start+entry.struct.SizeOfData]
            if raw.startswith(b'RSDS'):
                debug.append(dict(file_offset=start+24,path=raw[24:].split(b'\0',1)[0].decode('utf-8','replace')))
    directory=pe.OPTIONAL_HEADER.DATA_DIRECTORY[4]
    certificates=[]
    if directory.VirtualAddress:
        from cryptography.hazmat.primitives.serialization import pkcs7
        length,revision,kind=struct.unpack_from('<IHH',data,directory.VirtualAddress)
        if length<8 or directory.VirtualAddress+length>len(data):raise ValueError('Invalid certificate extent')
        # Some PKCS#7 blobs include zero alignment padding after the DER object.
        raw=data[directory.VirtualAddress+8:directory.VirtualAddress+length]
        if len(raw)<2 or raw[0]!=0x30:raise ValueError('Expected PKCS#7 ASN.1 sequence')
        width=raw[1]&127 if raw[1]&128 else 0
        if width>4 or (raw[1]&128 and not width):raise ValueError('Unsupported ASN.1 length')
        end=(2+width+int.from_bytes(raw[2:2+width],'big')) if width else 2+raw[1]
        if end>len(raw) or any(raw[end:]):raise ValueError('Invalid certificate padding')
        raw=raw[:end]
        for cert in pkcs7.load_der_pkcs7_certificates(raw):
            certificates.append(dict(subject=cert.subject.rfc4514_string(),issuer=cert.issuer.rfc4514_string()))
    return dict(machine=hex(pe.FILE_HEADER.Machine),subsystem=pe.OPTIONAL_HEADER.Subsystem,
                pe_timestamp_untrusted=pe.FILE_HEADER.TimeDateStamp,
                version_resource=versions,imports=imports,exports=exports,pdb_paths=debug,
                certificate_table_present=bool(directory.VirtualAddress),
                embedded_certificate_subjects=certificates,
                authenticode_signature_verified=False)

def literal_locations(data,selected):
    result=[]
    for text in sorted(selected):
        for encoding in ('ascii','utf-16le'):
            needle=text.encode(encoding)
            for match in re.finditer(re.escape(needle),data):
                result.append(dict(text=text,encoding=encoding,file_offset=match.start()))
    return result

def managed_metadata(data):
    import dnfile
    from dncil.cil.body.reader import read_method_body_from_bytes
    from dncil.clr.token import Token,StringToken
    pe=dnfile.dnPE(data=data)
    method_owners={};field_owners={}
    for t in pe.net.mdtables.TypeDef:
        name=f'{t.TypeNamespace}.{t.TypeName}'.lstrip('.')
        for m in t.MethodList:method_owners[m.row_index]=name
        for f in t.FieldList:field_owners[f.row_index]=name
    def token_name(token):
        table=pe.net.mdtables.tables.get(token.table)
        if table is None or not 1<=token.rid<=len(table.rows):return hex(token.value)
        row=table.rows[token.rid-1]
        if token.table==43: # MethodSpec: retain the underlying generic method name.
            inner=row.Method
            return token_name(Token((inner.table.number<<24)|inner.row_index))
        name=str(getattr(row,'Name',getattr(row,'TypeName','')))
        if token.table==6:return method_owners[token.rid]+'::'+name
        if token.table==4:return field_owners[token.rid]+'::'+name
        if hasattr(row,'Class') and row.Class and row.Class.row:
            parent=row.Class.row
            owner=(str(getattr(parent,'TypeNamespace',''))+'.'+str(getattr(parent,'TypeName',''))).strip('.')
            if owner:return owner+'::'+name
        return name
    enums={};fields=[]
    for constant in pe.net.mdtables.Constant:
        if constant.Parent.table.name!='Field':continue
        owner=field_owners.get(constant.Parent.row_index,'')
        if owner not in {'SecureAttestationWizard.Models.Check','SecureAttestationWizard.Models.CheckCategory','SecureAttestationWizard.Models.Level'}:continue
        enums.setdefault(owner,{})[str(constant.Parent.row.Name)]=int.from_bytes(constant.Value.value,'little',signed=True)
    for t in pe.net.mdtables.TypeDef:
        if str(t.TypeName)=='CheckInfo':
            fields=[dict(name=str(f.row.Name),token=hex(0x04000000|f.row_index),
                         signature_hex=f.row.Signature.value.hex()) for f in t.FieldList]
    pinvokes=[dict(module=str(i.ImportScope.row.Name),entry_point=str(i.ImportName),
                   managed_method=str(i.MemberForwarded.row.Name),
                   token=hex(0x06000000|i.MemberForwarded.row_index))
              for i in pe.net.mdtables.ImplMap]
    methods=[]
    wanted={'calculateData','GetDiagnosticInfo','BuildCertreqCommandArguments','StoreAikEnrollmentResult'}
    for number,method in enumerate(pe.net.mdtables.MethodDef,1):
        owner=method_owners[number];name=str(method.Name)
        if not method.Rva or not (name in wanted or
            (name=='get_IsPassed' and owner=='SecureAttestationWizard.ViewModels.CheckInfoViewModel') or
            (name=='MoveNext' and any(s in owner for s in ['<GenerateAikKey>','<CreateDiagnosticFileAsync>']))):continue
        body=read_method_body_from_bytes(pe.get_data(method.Rva))
        refs=[]
        for instruction in body.instructions:
            operand=instruction.operand;opcode=instruction.opcode.name
            if name=='get_IsPassed' and opcode=='ceq':
                refs.append(dict(il_offset=instruction.offset-body.header_size,opcode=opcode))
            if name=='calculateData' and opcode=='ldc.i4.s':
                refs.append(dict(il_offset=instruction.offset-body.header_size,opcode=opcode,constant=operand))
            if name=='calculateData' and opcode=='newarr' and isinstance(operand,Token):
                refs.append(dict(il_offset=instruction.offset-body.header_size,opcode=opcode,
                                 token=hex(operand.value),target=token_name(operand)))
            if isinstance(operand,StringToken):
                text=str(pe.net.user_strings.get(operand.rid))
                # Report identifiers/paths and enrollment arguments, not UI prose.
                if len(text)>160 or not re.search(r'\.exe|\.json|\.txt|\.zip|enrollaik|Activision|EnrollAIK|PCPKSP|/c |config',text):continue
                refs.append(dict(il_offset=instruction.offset-body.header_size,opcode=opcode,
                                 token=hex(operand.value),string=text))
            elif isinstance(operand,Token) and opcode in {'call','callvirt','newobj'}:
                name_ref=token_name(operand)
                if any(s in name_ref for s in ['AsyncTaskMethodBuilder','Awaiter','DefaultInterpolatedStringHandler','System.String::','System.Exception::']):continue
                refs.append(dict(il_offset=instruction.offset-body.header_size,opcode=opcode,
                                 token=hex(operand.value),target=name_ref))
        methods.append(dict(owner=owner,name=name,token=hex(0x06000000|number),
                            rva=method.Rva,file_offset=pe.get_offset_from_rva(method.Rva),
                            il_header_size=body.header_size,il_size=body.code_size,
                            code_sha256=sha(pe.get_data(method.Rva+body.header_size,body.code_size)),
                            references=refs))
    return dict(enums=enums,check_info_fields=fields,pinvokes=pinvokes,selected_methods=methods,
                offsets='File offsets refer to the decompressed component; IL offsets start at the method instruction stream.')

def inspect(path,expected_sha256):
    if path.stat().st_size>LIMIT:raise ValueError('Archive exceeds 128 MiB limit')
    raw=path.read_bytes()
    if sha(raw)!=expected_sha256:raise ValueError('Archive SHA-256 mismatch; refusing to label another build')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names=archive.namelist()
        if names.count(ZIP_MEMBER)!=1:raise ValueError('Expected exactly one wizard executable')
        info=archive.getinfo(ZIP_MEMBER)
        if info.file_size>LIMIT:raise ValueError('Executable exceeds size limit')
        data=archive.read(info)
    manifest=bundle(data);components={}
    for entry in manifest['entries']:
        if entry['path'] not in TARGETS:continue
        content=component_bytes(data,entry)
        record=dict(sha256=sha(content),bytes=len(content),bundle_entry=entry)
        if entry['path'].endswith('.json'):record['json']=json.loads(content.decode('utf-8-sig'))
        else:
            record['pe']=pe_metadata(content)
            if entry['path']=='CODSecureAttestationWizard.dll':record['managed']=managed_metadata(content)
            if entry['path'] in SELECTED_STRINGS:record['literals']=literal_locations(content,SELECTED_STRINGS[entry['path']])
        components[entry['path']]=record
    return dict(report_schema='wizard-static-1.0',analysis='Static file parsing only; no contained code executed.',
                archive=dict(name=path.name,sha256=sha(raw),bytes=len(raw),members=names),
                executable=dict(sha256=sha(data),bytes=len(data),pe=pe_metadata(data)),
                bundle={k:v for k,v in manifest.items() if k!='entries'},
                bundle_paths=[e['path'] for e in manifest['entries']],components=components,
                analyst_packages={n:importlib.metadata.version(n) for n in ['pefile','dnfile','dncil','cryptography']},
                limits=['Diagnostic utility only; not the RICOCHET kernel driver or current game client.',
                        'Imports and strings establish file contents, not execution frequency or detection policy.',
                        'Certificate subjects are metadata; signature, chain, timestamp and revocation were not verified.',
                        'PE timestamps/version resources do not establish publication dates.'])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('archive',type=Path)
    p.add_argument('--expected-sha256',required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    try:report=inspect(a.archive,a.expected_sha256)
    except (ValueError,OSError,zipfile.BadZipFile,ImportError) as e:
        p.exit(2,f'{type(e).__name__}: {e}\n')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(dict(report=str(a.out),sha256=sha(a.out.read_bytes()),components=len(report['components']))))

if __name__=='__main__':main()
