# One executable per tst_*.cpp, each its own CTest case, laid out to mirror the
# sources they cover.
#
# AUTOMOC is what QTest needs: every test declares a Q_OBJECT class and includes
# the moc output at the end of its own file.

find_package(Qt6 REQUIRED COMPONENTS Test)

set(CMAKE_AUTOMOC ON)

set(AAP_TESTS battery case_advert encrypted head_tracking packets proximity proximity_keys)
set(BT_TESTS rpa)
set(CORE_TESTS battery_store key_store link_retry options status_line)

# The discovery test talks to BlueZ, so it needs the same sd-bus the app does
# and a fake to talk to. Linux only: there is no BlueZ to fake anywhere else.
if(NOT WIN32)
    list(APPEND BT_TESTS discovery)
endif()

function(add_aap_test directory name)
    add_executable(tst_${name} "${directory}/tst_${name}.cpp")
    target_include_directories(tst_${name} PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_${name} PRIVATE Qt6::Test OpenSSL::Crypto)
    if(NOT MSVC)
        target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wshadow)
    endif()
    # Named for where it sits, so ctest -R aap runs one layer of them.
    add_test(NAME ${directory}/${name} COMMAND tst_${name})
endfunction()

foreach(test IN LISTS AAP_TESTS)
    add_aap_test(aap ${test})
endforeach()

foreach(test IN LISTS BT_TESTS)
    add_aap_test(bt ${test})
endforeach()

foreach(test IN LISTS CORE_TESTS)
    add_aap_test(core ${test})
endforeach()

if(NOT WIN32)
    target_link_libraries(tst_discovery PRIVATE PkgConfig::SYSTEMD)
    target_compile_definitions(tst_discovery PRIVATE
        FAKE_BLUEZ_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/bt/fake_bluez.py")
endif()
