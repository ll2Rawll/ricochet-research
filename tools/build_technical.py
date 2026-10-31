#!/usr/bin/env python3
"""Build the focused MW4/Warzone JSONL from reviewed core claims and static data."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
REPORT='technical/wizard-1.3.0-static.json'
ARCHIVE_URL='https://storage.googleapis.com/atvi-support-psapi-prod/CODSecureAttestationWizard_1.3.0.zip'
CORE_IDS=[2,3,4,6,59,62,83,84,94,101,102,110,111,112,113,114,115,132,
          145,148,149,151,161,162,167,168,169,170,174,183,184,186,187,188,189,190,
          221,222,224,225,227]+list(range(229,243))

def pointer(document,path):
    value=document
    for part in path.split('/')[1:]:
        part=part.replace('~1','/').replace('~0','~')
        value=value[int(part)] if isinstance(value,list) else value[part]
    return value

def build():
    core_raw=(ROOT/'ricochet.jsonl').read_bytes()
    core=[json.loads(l) for l in core_raw.splitlines()]
    claims={r['id']:r for r in core if r['type']=='claim'}
    selected={f'C{n:03d}' for n in CORE_IDS};pending=list(selected)
    while pending:
        c=claims[pending.pop()]
        for dependency in c.get('basis_claims',[])+c.get('updates',[])+c.get('contrasts_with',[]):
            if dependency not in selected:selected.add(dependency);pending.append(dependency)
    report_raw=(ROOT/REPORT).read_bytes();report=json.loads(report_raw)
    if report['archive']['sha256']!='75a49a5743cc975fed640a65e333419f45caa544bc93a2d0d55972d660d0b1b2':
        raise ValueError('This review is pinned to the official 1.3.0 archive')
    if report['executable']['sha256']!='5bfbedb08b94cefae839216c8df04384a679e157f280b81c889acc89325d4d06':
        raise ValueError('Reviewed executable identity changed')
    managed='/components/CODSecureAttestationWizard.dll/managed'
    native='/components/SecureAttestation.dll'
    methods=report['components']['CODSecureAttestationWizard.dll']['managed']['selected_methods']
    def method_path(name,owner=None):
        matches=[i for i,m in enumerate(methods) if m['name']==name and (owner is None or owner in m['owner'])]
        if len(matches)!=1:raise ValueError('Expected one reviewed method: '+name)
        return managed+'/selected_methods/'+str(matches[0])
    def exact(path,expected):return dict(report_pointer=path,expected=expected)
    def contains(path,values):return dict(report_pointer=path,contains=values)
    observations=[]
    def add(number,topic,text,evidence):
        for item in evidence:
            value=pointer(report,item['report_pointer'])
            if 'expected' in item and value!=item['expected']:raise ValueError('Reviewed value changed: '+item['report_pointer'])
            if 'contains' in item and not all(v in value for v in item['contains']):raise ValueError('Reviewed members changed: '+item['report_pointer'])
        observations.append(dict(type='claim',id=f'T{number:03d}',topic=topic,status='static_observation',
            as_of='2026-10-31',scope='Official Secure Attestation Wizard 1.3.0 artifact only; not a game/kernel-driver build.',
            text=text,sources=['A01','S27'],locator=REPORT+'#'+evidence[0]['report_pointer'],
            verification=dict(result='static_file_content_observed',checked_on='2026-10-31',
                              artifact='A01',report=REPORT,evidence=evidence)))
    add(1,'artifact_packaging','The ZIP contains one wizard executable. Its .NET bundle manifest lists 382 entries, including managed UI code, SecureAttestation.dll and enrollaik.exe.',[
        exact('/archive/members',['CODSecureAttestationWizard/','CODSecureAttestationWizard/CODSecureAttestationWizard.exe']),
        exact('/bundle/entry_count',382),contains('/bundle_paths',['SecureAttestation.dll','enrollaik.exe','CODSecureAttestationWizard.dll'])])
    add(2,'artifact_provenance','Embedded version.json identifies release 1.3.0 dated 2026-10-21. The outer executable version resource says 1.0.0.0; these are different metadata fields.',[
        exact('/components/version.json/json',{'Version':{'Major':1,'Minor':3,'Bugfix':0},'Date':'2026-10-21'}),
        exact('/executable/pe/version_resource/FileVersion','1.0.0.0')])
    add(3,'attestation_native','SecureAttestation.dll exports SecureAttestationReadinessVerify at RVA 0x7a60 and SecureAttestationGetDiagnosticInfo at RVA 0x8d40.',[
        exact(native+'/pe/exports',report['components']['SecureAttestation.dll']['pe']['exports'])])
    checks=pointer(report,managed+'/enums/SecureAttestationWizard.Models.Check')
    add(4,'attestation_managed','The managed Check enum names 13 readiness items (0–12) and COUNT=13. These are checker identifiers, not kernel detection vectors.',[
        exact(managed+'/enums/SecureAttestationWizard.Models.Check',checks)])
    passed=method_path('get_IsPassed')
    add(5,'attestation_managed','CheckInfo declares check, checkCategory, value, desiredValue and level. The view-model pass getter compares value with desiredValue; success does not mean every Boolean is true.',[
        exact(managed+'/check_info_fields',pointer(report,managed+'/check_info_fields')),
        exact(managed+'/enums/SecureAttestationWizard.Models.CheckCategory',{'TPM':0,'ATTESTATION':1,'SECURE_BOOT':2,'COUNT':3}),
        exact(managed+'/enums/SecureAttestationWizard.Models.Level',{'REQUIRED':0,'EXPECTED':1,'INFORMATIONAL':2,'COUNT':3}),
        exact(passed+'/token','0x6000061'),exact(passed+'/references',pointer(report,passed+'/references'))])
    add(6,'attestation_native','The native diagnostic library imports TPM device/context/command and TCG-log APIs from tbs.dll, plus key-provider/open-key/property APIs from ncrypt.dll. Import presence does not prove a runtime path.',[
        contains(native+'/pe/imports/tbs.dll',['Tbsi_GetDeviceInfo','Tbsi_Context_Create','Tbsip_Submit_Command','Tbsi_Get_TCG_Log','Tbsi_Get_TCG_Log_Ex','Tbsip_Context_Close']),
        contains(native+'/pe/imports/ncrypt.dll',['NCryptOpenStorageProvider','NCryptOpenKey','NCryptGetProperty','NCryptFreeObject'])])
    add(7,'attestation_native','Other native imports include GetFirmwareEnvironmentVariableW, DeviceIoControl, registry queries and service control. They establish available APIs, not a particular driver IOCTL or scan policy.',[
        contains(native+'/pe/imports/KERNEL32.dll',['GetFirmwareEnvironmentVariableW','DeviceIoControl']),
        contains(native+'/pe/imports/ADVAPI32.dll',['RegQueryValueExW','OpenServiceW','QueryServiceConfigW','StartServiceW'])])
    add(8,'attestation_keys','The diagnostic DLL contains ActivisionAIK, PCP_TPM_FW_VERSION, PCP_TPM_MANUFACTURER_ID and COD.Broker.Service literals. Each has a decompressed-file offset in the report.',[
        exact(native+'/literals',pointer(report,native+'/literals'))])
    calculate=method_path('calculateData')
    add(9,'attestation_managed','calculateData allocates 13 CheckInfo entries and contains a call to the managed P/Invoke declaration for SecureAttestationReadinessVerify at IL offset 0x11.',[
        exact(calculate+'/token','0x60000fc'),exact(calculate+'/references',pointer(report,calculate+'/references'))])
    diagnostic=method_path('GetDiagnosticInfo')
    add(10,'attestation_managed','GetDiagnosticInfo contains two native diagnostic calls: the first obtains a size, the second fills a buffer; later code decodes the returned data as UTF-8.',[
        exact(diagnostic+'/token','0x6000042'),exact(diagnostic+'/references',pointer(report,diagnostic+'/references'))])
    generate=method_path('MoveNext','<GenerateAikKey>')
    add(11,'attestation_managed','The GenerateAikKey async state machine combines the application directory with enrollaik.exe, checks file existence and calls RunElevatedAsync. This is a static call path, not an observed execution.',[
        exact(generate+'/token','0x600013d'),exact(generate+'/references',pointer(report,generate+'/references'))])
    store=method_path('StoreAikEnrollmentResult')
    add(12,'attestation_managed','StoreAikEnrollmentResult contains the Software\\Activision\\Call Of Duty path, EnrollAIK value name and RegistryKey.SetValue call. This does not establish a hardware-ban identifier.',[
        exact(store+'/token','0x6000083'),exact(store+'/references',pointer(report,store+'/references'))])
    add(13,'attestation_keys','The bundled enrollment helper contains ActivisionAIK and a certreq.exe -enrollaik machine-enrollment command prefix. Strings alone do not prove every branch executes or that AIKs are ban-registry keys.',[
        exact('/components/enrollaik.exe/literals',pointer(report,'/components/enrollaik.exe/literals'))])
    add(14,'artifact_packaging','The runtime configuration declares net9.0 and included Microsoft.NETCore.App/Microsoft.WindowsDesktop.App frameworks version 9.0.5.',[
        exact('/components/CODSecureAttestationWizard.runtimeconfig.json/json/runtimeOptions/tfm','net9.0'),
        exact('/components/CODSecureAttestationWizard.runtimeconfig.json/json/runtimeOptions/includedFrameworks',[
            {'name':'Microsoft.NETCore.App','version':'9.0.5'},{'name':'Microsoft.WindowsDesktop.App','version':'9.0.5'}])])
    certs=report['components']['enrollaik.exe']['pe']['embedded_certificate_subjects']
    add(15,'artifact_provenance','The enrollment helper contains certificate metadata naming Activision Publishing Inc. This review did not verify Authenticode signatures, trust chains, timestamps or revocation.',[
        exact('/components/enrollaik.exe/pe/certificate_table_present',True),
        exact('/components/enrollaik.exe/pe/embedded_certificate_subjects',certs),
        exact('/components/enrollaik.exe/pe/authenticode_signature_verified',False)])
    add(16,'artifact_packaging','The parsed bundle manifest has no entry named Randgrid.sys. This utility inspection does not inspect or certify the RICOCHET kernel driver.',[
        dict(report_pointer='/bundle_paths',absent=['Randgrid.sys'])])
    artifact=dict(type='source',id='A01',kind='vendor_diagnostic_artifact',
        title='Official Secure Attestation Wizard 1.3.0: static artifact inspection',url=ARCHIVE_URL,
        accessed='2026-10-31',published='2026-10-21',scope_source='S27',
        archive=report['archive'],executable_sha256=report['executable']['sha256'],
        components={k:dict(sha256=v['sha256'],bytes=v['bytes']) for k,v in report['components'].items()},
        report=REPORT,report_sha256=hashlib.sha256(report_raw).hexdigest(),
        note='Downloaded from the link on the official support page. Only static parsing performed. Publication field is the listing/embedded release date, not the storage object timestamp. No proprietary binary is redistributed.')
    used={s for id in selected for s in claims[id]['sources']}|{'S27'}
    sources=[r for r in core if r['type']=='source' and r['id'] in used]+[artifact]
    focused=[r for r in core if r['type']=='claim' and r['id'] in selected]
    meta=dict(type='meta',schema_version='mw4-warzone-technical-1.0',revision=6,researched_on='2026-10-31',
        subject='MW4 and Warzone: anti-cheat technical evidence',
        scope='Attributed game statements, explicitly dated history, Microsoft platform/sample code, and build-identified official diagnostic-utility observations.',
        completeness='No proprietary kernel/game source, current driver trace, private IOCTL definitions, visibility algorithm, DMA algorithm, model weights or ban-policy internals were verified.',
        reading_order='Read scope and status first. C IDs retain core citations; T IDs are static observations of A01. Resolve report JSON pointers for exact metadata and code locations.',
        statuses={**core[0]['statuses'],'static_observation':'Reproduced file contents of a hash-identified public diagnostic artifact; no claim of game-driver runtime behavior.'},
        parent_sha256=hashlib.sha256(core_raw).hexdigest(),source_count=len(sources),claim_count=len(focused)+len(observations),
        core_claim_count=len(focused),static_observation_count=len(observations),artifact_source_count=1)
    output=[meta]+sources+focused+observations
    (ROOT/'technical/mw4-warzone.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False,separators=(',',':'))+'\n' for r in output))
    provenance=dict(reviewed_on='2026-10-31',official_link_source='S27',
        support_url='https://support.activision.com/articles/trusted-platform-module-and-secure-boot#AttestationWizard',
        download_url=ARCHIVE_URL,archive_sha256=report['archive']['sha256'],archive_bytes=report['archive']['bytes'],
        storage_object_last_modified='2026-10-22T02:13:35Z',storage_object_generation='1792635215204945',
        release_metadata_date='2026-10-21',report=REPORT,report_sha256=artifact['report_sha256'],
        format_references=[
            'https://github.com/dotnet/runtime/blob/v9.0.4/src/installer/managed/Microsoft.NET.HostModel/Bundle/Manifest.cs',
            'https://github.com/dotnet/runtime/blob/v9.0.4/src/installer/managed/Microsoft.NET.HostModel/Bundle/FileEntry.cs'],
        method='ZIP + .NET format-6 manifest + bounded DEFLATE + PE/CLR metadata + selected IL call/string references; no Windows executable loaded or run.',
        authenticity_limits=report['limits'])
    (ROOT/'technical/provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(json.dumps(dict(core_claims=len(focused),static_observations=len(observations),sources=len(sources),bytes=(ROOT/'technical/mw4-warzone.jsonl').stat().st_size)))

if __name__=='__main__':build()
