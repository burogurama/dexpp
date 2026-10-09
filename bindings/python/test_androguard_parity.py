# -*- coding: utf-8 -*-
"""Parity tests porting androguard's correctness suite onto dexpp.

Ported from androguard v4.1.4 (tag v4.1.4, commit d594fd3), Apache-2.0.
Source tests: androguard/tests/{test_dex,test_analysis,test_callgraph,
test_annotations,test_strings,test_apk}.py. Each test below re-asserts the
SAME concrete fact androguard asserts, but through dexpp's Python API.

Fixtures are vendored from androguard's tests/data/APK/ into
tests/data/androguard/ (Apache-2.0, test data only).

Run from the project root (fixtures are referenced relatively):

    PYTHONPATH=build python3 bindings/python/test_androguard_parity.py

Also runnable under pytest with the same PYTHONPATH.

Where dexpp is correct-but-different from androguard by design, the assertion
is adjusted to dexpp's documented behavior and the divergence is commented
inline with the marker "DIVERGENCE:". Where dexpp appears genuinely wrong, the
test is marked with "DEXPP BUG:" and kept as a real (failing) check.
"""

import re

import dexpp

D = "tests/data/androguard/"


# ---------------------------------------------------------------------------
# test_analysis.py :: testAPK  (a2dp.Vol_137.apk)
# Whole-program counts straight off the DEX header.
# ---------------------------------------------------------------------------

def test_analysis_a2dp_counts():
    ctx = dexpp.AnalysisContext.from_apk(D + "a2dp.Vol_137.apk")

    # header->classDefsSize: number of internal (defined) classes.
    # androguard: get_internal_classes() == 1353
    assert len(ctx.classes) == 1353

    # header->stringIdsSize: size of the string pool.
    # androguard: len(get_strings()) == 13523
    assert len(ctx.strings) == 13523

    # header->methodIdsSize: total method references (internal + external).
    # androguard counts get_internal_methods()==9676, get_external_methods()==3116,
    # get_methods()==12792 -- but it documents (test_analysis.py:52-54) that the
    # header actually says 12795 and that 3116 is "off"; JADX corroborates 9676.
    g = ctx.call_graph
    resolved = sum(1 for n in g.nodes if n.resolved)
    # internal (resolved) methods match androguard / JADX exactly.
    assert resolved == 9676
    # DIVERGENCE: dexpp's total call-graph nodes == header methodIdsSize == 12795,
    # i.e. it is header-faithful where androguard's 12792 under-counts by 3 (a known
    # androguard inaccuracy noted in its own test). So dexpp == 12795, not 12792.
    assert len(g.nodes) == 12795
    assert sum(1 for n in g.nodes if not n.resolved) == 12795 - 9676  # 3119 external

    # find_classes("^(?!Landroid/support).*;$", no_external=True) == 124
    pat = re.compile(r"^(?!Landroid/support).*;$")
    names = [c.name for c in ctx.classes]
    assert sum(1 for n in names if pat.match(n)) == 124

    # find_methods(classname="^(?!Landroid).*;$", methodname="<init>",
    #              descriptor=r"^\(.+\).*$", no_external=True) == 94
    mpat = re.compile(r"^(?!Landroid).*;$")
    dpat = re.compile(r"^\(.+\).*$")
    init_count = 0
    for c in ctx.classes:
        if not mpat.match(c.name):
            continue
        for m in c.methods:
            if m.name != "<init>":
                continue
            proto = "(" + "".join(m.parameters) + ")" + m.return_type
            if dpat.match(proto):
                init_count += 1
    assert init_count == 94

    # find_strings(r".*://.*") == 16
    url = re.compile(r".*://.*")
    assert sum(1 for s in ctx.strings if url.match(s)) == 16


# ---------------------------------------------------------------------------
# test_analysis.py :: testAnalysis  (AnalysisTest.dex)
# ---------------------------------------------------------------------------

def test_analysis_analysistest_dex():
    ctx = dexpp.AnalysisContext.from_dex(D + "AnalysisTest.dex")

    # 1 internal class, 4 internal methods, 21 strings, 0 fields.
    assert len(ctx.classes) == 1
    cls = ctx.classes[0]
    assert len(cls.methods) == 4  # androguard get_internal_methods()==4
    assert len(cls.fields) == 0
    assert len(ctx.strings) == 21

    # androguard: 4 external classes appear in get_external_classes().
    # dexpp models them as descriptor-only (not-loaded) hierarchy nodes.
    h = ctx.class_hierarchy
    for ext in [
        "Ljava/io/PrintStream;",
        "Ljava/lang/Object;",
        "Ljava/math/BigDecimal;",
        "Ljava/math/BigInteger;",
    ]:
        # Referenced but not defined in this DEX -> external.
        assert not h.is_loaded(ext)
    # And every external class is referenced from the one internal class's code.
    referenced_types = set()
    for m in cls.methods:
        for tu in m.type_uses:
            referenced_types.add(tu.descriptor)
        for c in m.calls:
            referenced_types.add(c.target.class_descriptor)
    for ext in ["Ljava/math/BigDecimal;", "Ljava/math/BigInteger;",
                "Ljava/io/PrintStream;"]:
        assert ext in referenced_types


# ---------------------------------------------------------------------------
# test_analysis.py :: testInterfaces / testExtends
# ---------------------------------------------------------------------------

def test_analysis_interfaces():
    ctx = dexpp.AnalysisContext.from_dex(D + "InterfaceCls.dex")
    cls = ctx.find_class("LInterfaceCls;")
    assert cls is not None
    assert cls.name == "LInterfaceCls;"
    impls = [i.descriptor for i in cls.interfaces]
    assert "Ljavax/net/ssl/X509TrustManager;" in impls


def test_analysis_extends():
    ctx = dexpp.AnalysisContext.from_dex(D + "ExceptionHandling.dex")
    cls = ctx.find_class("LSomeException;")
    assert cls is not None
    assert cls.name == "LSomeException;"
    assert cls.superclass.descriptor == "Ljava/lang/Exception;"

    h = ctx.class_hierarchy
    # androguard: Ljava/lang/Exception; extends Ljava/lang/Object;, no implements,
    # and is_external() == True.
    assert h.supertypes("LSomeException;") == ["Ljava/lang/Exception;"]
    # DIVERGENCE: java.lang.Exception is not defined in this DEX, so dexpp marks it
    # not-loaded (androguard's is_external() == True is the same fact). dexpp does
    # not synthesize its Object supertype because the class body is absent.
    assert not h.is_loaded("Ljava/lang/Exception;")


# ---------------------------------------------------------------------------
# test_analysis.py :: testMultidex / testMultiDexExternal  (multidex.apk)
# ---------------------------------------------------------------------------

def test_analysis_multidex():
    ctx = dexpp.AnalysisContext.from_apk(D + "multidex.apk")
    names = [c.name for c in ctx.classes]
    # both classes live across classes.dex / classes2.dex
    assert "Lcom/foobar/foo/Foobar;" in names
    assert "Lcom/blafoo/bar/Blafoo;" in names

    # When both DEX files are loaded, neither cross-DEX reference is external.
    h = ctx.class_hierarchy
    assert h.is_loaded("Lcom/foobar/foo/Foobar;")
    assert h.is_loaded("Lcom/blafoo/bar/Blafoo;")


# ---------------------------------------------------------------------------
# test_analysis.py :: testXrefs  (classes.dex)
# Forward xrefs (get_xref_to) and reverse xrefs (get_xref_from).
# ---------------------------------------------------------------------------

def _full(mid):
    # androguard full_name is "<class> <name> (<spaced-proto>)" -- dexpp protos are
    # unspaced (documented divergence). Compare the structured triple instead.
    return (mid.class_descriptor, mid.name, mid.proto)


def test_xrefs_oncreate_forward():
    ctx = dexpp.AnalysisContext.from_dex(D + "classes.dex")
    cls = ctx.find_class("Ltests/androguard/TestActivity;")
    onCreate = next(m for m in cls.methods if m.name == "onCreate")

    # androguard get_xref_to() sorted by bytecode offset -> exactly these 5 targets,
    # in this order. dexpp Method.calls() preserves code order.
    calls = onCreate.calls
    seq = [(c.target.class_descriptor, c.target.name, c.target.proto, c.kind)
           for c in calls]
    assert seq == [
        ("Landroid/app/Activity;", "onCreate", "(Landroid/os/Bundle;)V",
         dexpp.InvokeKind.SUPER),
        ("Ltests/androguard/TestActivity;", "setContentView", "(I)V",
         dexpp.InvokeKind.VIRTUAL),
        ("Ltests/androguard/TestActivity;", "getApplicationContext",
         "()Landroid/content/Context;", dexpp.InvokeKind.VIRTUAL),
        # DIVERGENCE: dexpp renders proto descriptors UNSPACED; androguard spaces
        # argument types ("(Landroid/content/Context; Ljava/lang/CharSequence; I)...").
        ("Landroid/widget/Toast;", "makeText",
         "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
         dexpp.InvokeKind.STATIC),
        ("Landroid/widget/Toast;", "show", "()V", dexpp.InvokeKind.VIRTUAL),
    ]


def test_xrefs_testcalls_forward():
    ctx = dexpp.AnalysisContext.from_dex(D + "classes.dex")
    cls = ctx.find_class("Ltests/androguard/TestActivity;")
    testCalls = next(m for m in cls.methods if m.name == "testCalls")
    seq = [(c.target.class_descriptor, c.target.name, c.target.proto)
           for c in testCalls.calls]
    assert seq == [
        ("Ltests/androguard/TestActivity;", "testCall2", "(J)V"),
        ("Ltests/androguard/TestIfs;", "testIF", "(I)I"),
        ("Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;"),
        ("Ljava/io/PrintStream;", "println", "(Ljava/lang/Object;)V"),
    ]


def test_xrefs_teststring_new_instance():
    ctx = dexpp.AnalysisContext.from_dex(D + "classes.dex")
    cls = ctx.find_class("Ltests/androguard/TestActivity;")
    testString = next(m for m in cls.methods if m.name == "testString")
    # androguard get_xref_new_instance(): String is new-instanced inside testString.
    new_types = {tu.descriptor for tu in testString.type_uses
                 if tu.kind == dexpp.TypeRefKind.NEW_INSTANCE}
    assert "Ljava/lang/String;" in new_types


def test_xrefs_reverse():
    ctx = dexpp.AnalysisContext.from_dex(D + "classes.dex")
    x = ctx.xrefs
    # androguard: TestActivity.onCreate is the (only) referrer of setContentView,
    # getApplicationContext, Toast.makeText, Toast.show, Activity.onCreate.
    onCreate_id = dexpp.MethodId(
        "Ltests/androguard/TestActivity;", "onCreate", "(Landroid/os/Bundle;)V")

    def referrers(cls, name, proto):
        mid = dexpp.MethodId(cls, name, proto)
        return {_full(x.referrer_id(r.referrer)) for r in x.method_refs(mid)}

    assert _full(onCreate_id) in referrers(
        "Ltests/androguard/TestActivity;", "setContentView", "(I)V")
    assert _full(onCreate_id) in referrers(
        "Ltests/androguard/TestActivity;", "getApplicationContext",
        "()Landroid/content/Context;")
    assert _full(onCreate_id) in referrers(
        "Landroid/widget/Toast;", "makeText",
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;")
    assert _full(onCreate_id) in referrers(
        "Landroid/widget/Toast;", "show", "()V")

    # testCalls is a referrer of testCall2, TestIfs.testIF, Object.getClass.
    # (its real descriptor takes a TestIfs argument).
    testCalls_id = dexpp.MethodId(
        "Ltests/androguard/TestActivity;", "testCalls",
        "(Ltests/androguard/TestIfs;)V")
    assert _full(testCalls_id) in referrers(
        "Ltests/androguard/TestActivity;", "testCall2", "(J)V")
    assert _full(testCalls_id) in referrers(
        "Ltests/androguard/TestIfs;", "testIF", "(I)I")
    assert _full(testCalls_id) in referrers(
        "Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;")


# ---------------------------------------------------------------------------
# test_analysis.py :: testXrefOffsets  (AnalysisTest.dex)
# ---------------------------------------------------------------------------

def test_xref_string_offset():
    ctx = dexpp.AnalysisContext.from_dex(D + "AnalysisTest.dex")
    x = ctx.xrefs
    sites = x.string_refs("Hello world")
    assert len(sites) == 1
    # DIVERGENCE: androguard reports the bytecode offset in BYTES (== 4 here);
    # dexpp reports it in 16-bit code units (== 2). Consistent factor of 2.
    assert sites[0].code_offset == 2
    assert x.referrer_id(sites[0].referrer).name == "testStaticCalls"


# ---------------------------------------------------------------------------
# test_analysis.py :: testXrefOffsetsFields  (FieldsTest.dex)
# ---------------------------------------------------------------------------

def test_xref_field_offsets():
    ctx = dexpp.AnalysisContext.from_dex(D + "FieldsTest.dex")
    assert len(ctx.strings) == 20
    for s in ["hello world", "sdf", "hello mars", "i am static"]:
        assert s in ctx.strings

    x = ctx.xrefs
    afield = dexpp.FieldId("LFieldsTest;", "afield", "Ljava/lang/String;")

    reads = x.field_reads(afield)
    # androguard: 2 read sites, both in "foonbar", byte offsets [4, 40].
    # DIVERGENCE (units): dexpp code-unit offsets are exactly half -> [2, 20].
    assert sorted(r.code_offset for r in reads) == [2, 20]
    assert [x.referrer_id(r.referrer).name for r in
            sorted(reads, key=lambda r: r.code_offset)] == ["foonbar", "foonbar"]

    writes = x.field_writes(afield)
    # androguard: 2 write sites in {<init>, foonbar}, byte offsets [10, 32].
    assert sorted(r.code_offset for r in writes) == [5, 16]
    assert sorted(x.referrer_id(r.referrer).name for r in writes) == \
        ["<init>", "foonbar"]

    # cfield is static -> written in <clinit>, read in foonbar.
    cfield = dexpp.FieldId("LFieldsTest;", "cfield", "Ljava/lang/String;")
    assert sorted(x.referrer_id(r.referrer).name for r in x.field_writes(cfield)) \
        == ["<clinit>"]
    assert sorted(x.referrer_id(r.referrer).name for r in x.field_reads(cfield)) \
        == ["foonbar"]


# ---------------------------------------------------------------------------
# test_dex.py :: testAccessflags  (TestActivity.apk)
# ---------------------------------------------------------------------------

def test_access_flags():
    ctx = dexpp.AnalysisContext.from_apk(D + "TestActivity.apk")

    loops = ctx.find_class("Ltests/androguard/TestLoops;")
    assert loops.access_flags == 0x1  # public
    loop_methods = {m.name: m.access_flags for m in loops.methods}
    assert loop_methods["<init>"] == 0x1 | 0x10000  # public | constructor
    for name in ["testBreak", "testWhile", "testFor", "testDoWhile"]:
        assert loop_methods[name] == 0x1

    inner = ctx.find_class("Ltests/androguard/TestLoops$Loop;")
    assert inner.access_flags == 0x1
    inner_methods = {m.name: m.access_flags for m in inner.methods}
    assert inner_methods["<init>"] == 0x4 | 0x10000  # protected | constructor
    inner_fields = {f.name: f.access_flags for f in inner.fields}
    assert inner_fields["i"] == 0x1 | 0x8  # public | static
    assert inner_fields["j"] == 0x1 | 0x8

    ifs = ctx.find_class("Ltests/androguard/TestIfs;")
    assert ifs.access_flags == 0x1
    ifs_methods = {m.name: m.access_flags for m in ifs.methods}
    assert ifs_methods["<init>"] == 0x1 | 0x10000
    assert ifs_methods["testIF"] == 0x1 | 0x8  # public | static
    assert ifs_methods["testCFG"] == 0x1  # public
    ifs_fields = {f.name: f.access_flags for f in ifs.fields}
    for name in ["P", "Q", "R", "S"]:
        assert ifs_fields[name] == 0x2  # private


# ---------------------------------------------------------------------------
# test_strings.py :: testDex  (StringTests.dex)
# MUTF-8 / CESU-8 / surrogate-pair correctness of the string pool.
# ---------------------------------------------------------------------------

def test_string_pool_mutf8():
    ctx = dexpp.AnalysisContext.from_dex(D + "StringTests.dex")
    strings = set(ctx.strings)
    stests = [
        "this is a quite normal string",
        "\u0000 \u0001 \u1234",  # embedded NUL (CESU-8 \xc0\x80) + control + CJK
        "使用在線工具將字符串翻譯為中文",                  # CJK
        "перевод строки на русский с помощью онлайн-инструментов",
        "온라인 도구를 사용하여 문자열을 한국어로 번역",       # Hangul
        "オンラインツールを使用して文字列を日本語に翻訳",        # Kana/CJK
        "This is \U0001f64f, an emoji.",            # supplementary plane (surrogate pair)
        "✓ check this string",
        "\uffff \u0000 \uff00",  # noncharacter U+FFFF + NUL + fullwidth
        "Россия",
    ]
    for s in stests:
        assert s in strings, "missing string: " + ascii(s)


# ---------------------------------------------------------------------------
# test_annotations.py :: testAnnotation  (Annotation_classes.dex)
# ---------------------------------------------------------------------------

def test_class_annotation_type_ids():
    ctx = dexpp.AnalysisContext.from_dex(D + "Annotation_classes.dex")
    cls = ctx.find_class(
        "Landroid/support/v4/widget/SlidingPaneLayout$SlidingPanelLayoutImplJB;")
    assert cls is not None
    types = [a.type_descriptor for a in cls.annotations]
    assert "Landroid/support/annotation/RequiresApi;" in types


# ---------------------------------------------------------------------------
# test_callgraph.py :: testCallgraph*  (TestActivity.apk)
# androguard's exact node/edge totals depend on its own filtering model; we port
# the structural facts that map cleanly to dexpp's call graph.
# ---------------------------------------------------------------------------

def test_callgraph_structure():
    ctx = dexpp.AnalysisContext.from_apk(D + "TestActivity.apk")
    g = ctx.call_graph
    assert not g.empty

    # androguard's testCallgraphFilterDescriptor isolates a single edge: a
    # synthetic constructor -> the matching private constructor of the same inner
    # class. In TestActivity.apk's DEX the deepest inner class carries this exact
    # synthetic/private constructor pair.
    inner = "LTestDefaultPackage$TestInnerClass$TestInnerInnerClass;"
    src = dexpp.MethodId(
        inner, "<init>",
        "(LTestDefaultPackage$TestInnerClass;IIL"
        "TestDefaultPackage$TestInnerClass$TestInnerInnerClass;)V")
    dst = dexpp.MethodId(
        inner, "<init>", "(LTestDefaultPackage$TestInnerClass;II)V")
    src_node = g.find(src)
    dst_node = g.find(dst)
    assert src_node is not None and src_node.resolved
    assert dst_node is not None and dst_node.resolved
    # synthetic ctor is marked synthetic+constructor; the dst is private+constructor.
    syn = next(m for m in ctx.find_class(inner).methods
               if m.name == "<init>" and (m.access_flags & 0x1000))  # synthetic
    assert syn.access_flags & 0x10000  # constructor
    priv = next(m for m in ctx.find_class(inner).methods
                if m.name == "<init>" and (m.access_flags & 0x2))  # private
    assert priv.access_flags & 0x10000

    # CallNode.outgoing holds EDGE indices into CallGraph.edges (not node indices);
    # the synthetic ctor calls the private ctor exactly once.
    callee_ids = {g.edges[e].callee_node for e in src_node.outgoing}
    assert dst_node.index in callee_ids
    # and the authoritative forward-ref view agrees.
    assert any(c.target == dst for c in syn.calls)


# ---------------------------------------------------------------------------
# test_apk.py :: testAPKManifest  (TestActivity.apk)
# ---------------------------------------------------------------------------

def test_manifest_testactivity():
    apk = dexpp.Apk.open(D + "TestActivity.apk")
    m = apk.manifest
    assert m is not None
    assert m.package == "tests.androguard"
    assert m.version_code == 1
    assert m.version_name == "1.0"
    assert m.min_sdk == 9
    assert m.target_sdk == 16
    assert m.permissions == []
    assert m.declared_permissions == []
    # androguard get_main_activity() == "tests.androguard.TestActivity"
    assert m.launcher_activity == "tests.androguard.TestActivity"
    acts = m.components_of(dexpp.ComponentKind.ACTIVITY)
    assert [c.name for c in acts] == ["tests.androguard.TestActivity"]


# ---------------------------------------------------------------------------
# test_apk.py :: testAPKPermissions / testAPKIntentFilters  (a2dp.Vol_137.apk)
# ---------------------------------------------------------------------------

def test_manifest_a2dp_permissions():
    apk = dexpp.Apk.open(D + "a2dp.Vol_137.apk")
    m = apk.manifest
    assert m.package == "a2dp.Vol"
    expected = sorted([
        "android.permission.RECEIVE_BOOT_COMPLETED",
        "android.permission.CHANGE_WIFI_STATE",
        "android.permission.ACCESS_WIFI_STATE",
        "android.permission.KILL_BACKGROUND_PROCESSES",
        "android.permission.BLUETOOTH",
        "android.permission.BLUETOOTH_ADMIN",
        "com.android.launcher.permission.READ_SETTINGS",
        "android.permission.RECEIVE_SMS",
        "android.permission.MODIFY_AUDIO_SETTINGS",
        "android.permission.READ_CONTACTS",
        "android.permission.ACCESS_COARSE_LOCATION",
        "android.permission.ACCESS_FINE_LOCATION",
        "android.permission.ACCESS_LOCATION_EXTRA_COMMANDS",
        "android.permission.WRITE_EXTERNAL_STORAGE",
        "android.permission.READ_PHONE_STATE",
        "android.permission.BROADCAST_STICKY",
        "android.permission.GET_ACCOUNTS",
    ])
    assert sorted(m.permissions) == expected


def test_manifest_a2dp_intent_filters():
    apk = dexpp.Apk.open(D + "a2dp.Vol_137.apk")
    m = apk.manifest

    # androguard: single LAUNCHER activity intent-filter (MAIN/LAUNCHER).
    launcher_filters = []
    for c in m.components_of(dexpp.ComponentKind.ACTIVITY):
        for f in c.intent_filters:
            launcher_filters.append((sorted(f.actions), sorted(f.categories)))
    assert (["android.intent.action.MAIN"],
            ["android.intent.category.LAUNCHER"]) in launcher_filters

    # androguard: the single service intent-filter is the
    # NotificationListenerService action.
    svc_filters = []
    for c in m.components_of(dexpp.ComponentKind.SERVICE):
        for f in c.intent_filters:
            svc_filters.append(sorted(f.actions))
    assert svc_filters == [
        ["android.service.notification.NotificationListenerService"]]

    # androguard receiver filters (BOOT_COMPLETED+MY_PACKAGE_REPLACED / HOME,
    # and APPWIDGET_UPDATE).
    rcv_filters = []
    for c in m.components_of(dexpp.ComponentKind.RECEIVER):
        for f in c.intent_filters:
            rcv_filters.append((sorted(f.actions), sorted(f.categories)))
    assert (["android.intent.action.BOOT_COMPLETED",
             "android.intent.action.MY_PACKAGE_REPLACED"],
            ["android.intent.category.HOME"]) in rcv_filters
    assert (["android.appwidget.action.APPWIDGET_UPDATE"], []) in rcv_filters


# ---------------------------------------------------------------------------
# test_apk.py :: testShortNamesInManifest  (AndroidManifest_ShortName.apk)
# ---------------------------------------------------------------------------

def test_manifest_short_names():
    apk = dexpp.Apk.open(D + "AndroidManifest_ShortName.apk")
    m = apk.manifest
    assert m.package == "com.android.galaxy4"
    acts = m.components_of(dexpp.ComponentKind.ACTIVITY)
    svcs = m.components_of(dexpp.ComponentKind.SERVICE)
    assert len(acts) == 1
    assert len(svcs) == 1
    # ".Galaxy4" short name qualified against the package.
    assert acts[0].name == "com.android.galaxy4.Galaxy4"
    assert svcs[0].name == "com.android.galaxy4.Galaxy4Wallpaper"


# ---------------------------------------------------------------------------
# test_apk.py :: testFrameworkResAPK  (lineageos_nexus5_framework-res.apk)
# ---------------------------------------------------------------------------

def test_manifest_framework_res():
    apk = dexpp.Apk.open(D + "lineageos_nexus5_framework-res.apk")
    m = apk.manifest
    assert m.package == "android"


# ---------------------------------------------------------------------------
# test_apk.py :: testCustomPermissionProtectionLevel  (tvleanback)
# ---------------------------------------------------------------------------

def test_manifest_custom_permission_protection_level():
    apk = dexpp.Apk.open(D + "com.example.android.tvleanback.apk")
    m = apk.manifest
    name = "com.example.android.tvleanback.ACCESS_VIDEO_DATA"
    assert name in m.declared_permissions
    # androguard get_details_permissions()[...][0] == 'signature'.
    # dexpp encodes protectionLevel numerically: 2 == signature.
    assert m.declared_permission_protection_level(name) == 2


# ---------------------------------------------------------------------------
# test_apk.py :: testAPKCertFingerprint  (TestActivity.apk, v1-only)
# ---------------------------------------------------------------------------

def test_signing_testactivity_v1():
    apk = dexpp.Apk.open(D + "TestActivity.apk")
    s = apk.signing
    assert s.is_signed
    assert s.v1
    assert not s.v2  # androguard: is_signed_v2() == False
    assert not s.v3
    assert len(s.certificates) == 1
    # keytool sha256 fingerprint from androguard's testAPKCertFingerprint.
    assert s.certificates[0].sha256_hex == \
        "6f5c31608f1f9e285eb6343c7c8af07de81c1fb2148b5349bec906444144576d"


# ---------------------------------------------------------------------------
# test_apk.py :: testAPKv2Signature  (TestActivity_signed_both.apk, v1+v2)
# ---------------------------------------------------------------------------

def test_signing_testactivity_both():
    apk = dexpp.Apk.open(D + "TestActivity_signed_both.apk")
    s = apk.signing
    assert s.v1 and s.v2
    assert s.is_signed
    # Signed with one certificate across both schemes -> de-duplicated to 1.
    assert len(s.certificates) == 1
    # androguard: the DER equals the on-disk certificate.der; its sha256 is stable.
    assert s.certificates[0].sha256_hex == \
        "b39038a91d8880fb01d2f6bdaeb22d39c1b7c447cef69e779bad544e9a3ec6a3"


# ---------------------------------------------------------------------------
# runner / pytest compatibility
# ---------------------------------------------------------------------------

def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failures = []
    for t in tests:
        try:
            t()
            print(f"PASS {t.__name__}")
        except Exception as e:  # noqa: BLE001
            failures.append((t.__name__, e))
            print(f"FAIL {t.__name__}: {type(e).__name__}: {e}")
    print()
    print(f"{len(tests) - len(failures)}/{len(tests)} tests passed")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
