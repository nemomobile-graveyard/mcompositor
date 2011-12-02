TEMPLATE = subdirs
SUBDIRS += ut_stacking ut_anim ut_lockscreen ut_closeapp ut_compositing \
           ut_netClientList ut_restackwindows ut_splashscreen ut_propcache

td    = /usr/share/test-definition/testdefinition
utdir = /usr/lib/mcompositor-unit-tests
suite   = mcompositor-tests
domain  = Application Framework
feature = MCompositor

tests_xml.target   = tests.xml
tests_xml.input    = miukumauku
tests_xml.output   = $${tests_xml.target}
tests_xml.depends  = unit.pro 
tests_xml.commands  = @if true; then
tests_xml.commands += echo
tests_xml.commands += \'<?xml version=\"1.0\" encoding=\"UTF-8\"?>
tests_xml.commands +=    <testdefinition version=\"0.1\">
tests_xml.commands +=      <suite name=\"$$suite\" domain=\"$$domain\"
tests_xml.commands +=             type=\"Functional\" level=\"Component\">
tests_xml.commands +=        <set name=\"unit_tests\"
tests_xml.commands +=             description=\"Unit Tests\"
tests_xml.commands +=             feature=\"$$feature\">\';
tests_xml.commands += for ut in $$SUBDIRS; do echo \'
tests_xml.commands +=          <case name=\"\'\$\$ut\'\" timeout=\"300\">
tests_xml.commands +=             <step>$$utdir/\'\$\$ut\'</step>
tests_xml.commands +=          </case>\';
tests_xml.commands += done;
tests_xml.commands += echo \'
tests_xml.commands +=          <environments>
tests_xml.commands +=            <scratchbox>false</scratchbox>
tests_xml.commands +=            <hardware>true</hardware>
tests_xml.commands +=          </environments>
tests_xml.commands +=        </set>
tests_xml.commands +=      </suite>
tests_xml.commands +=    </testdefinition>\';
tests_xml.commands += fi > $${tests_xml.output}
QMAKE_EXTRA_COMPILERS += tests_xml
QMAKE_EXTRA_TARGETS   += tests_xml

txml_inst.depends  = $${tests_xml.output}
txml_inst.files	   = $${tests_xml.output}
txml_inst.path     = /usr/share/mcompositor-unit-tests
txml_inst.CONFIG   = no_check_exist
INSTALLS += txml_inst
