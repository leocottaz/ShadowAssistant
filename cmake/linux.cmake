# Port Linux : GTK3 + WebKitGTK (webkit2gtk-4.1) + libcurl. Dépendances Debian/Ubuntu :
#   libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev   (Arch : gtk3 webkit2gtk-4.1 curl)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkit2gtk-4.1)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)   # raccourci global (XGrabKey)

find_package(Threads REQUIRED)

add_executable(ShadowAssistant
        src/linux/main_linux.cpp
        src/linux/dock_linux.cpp            # états, animations, souris, raccourci
        src/linux/bridge_linux.cpp          # routeur des commandes de la page
        src/linux/util_linux.cpp            # dossiers (XDG)
        src/linux/api_key_linux.cpp         # clés API (fichiers 0600)
        src/linux/anthropic_http_linux.cpp  # mode Clé API et Gratuit (libcurl)
        src/linux/claude_code_linux.cpp     # mode Abonnement Pro (fork/exec)
        src/linux/autostart_linux.cpp       # ~/.config/autostart
        src/linux/tray_linux.cpp            # notifications D-Bus
        src/linux/media_linux.cpp           # lecteur : MPRIS2
        src/linux/pc_linux.cpp              # fichiers, routines, volume
        src/features/reminders.cpp          # échéances des rappels et du minuteur (partagé avec Windows)
        src/features/updater_common.cpp     # version et vérification des mises à jour (partagé avec Windows)
        src/linux/updater_linux.cpp         # pas d'installation automatique sous Linux
        src/common/util_common.cpp)
target_include_directories(ShadowAssistant PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ShadowAssistant PRIVATE SHADOW_VERSION_STR="${SHADOW_VERSION_NUM}")
target_link_libraries(ShadowAssistant PRIVATE PkgConfig::GTK3 PkgConfig::WEBKIT PkgConfig::CURL PkgConfig::X11 Threads::Threads)

# L'interface se trouve à côté de l'exécutable.
add_custom_target(shadow_assets ALL
        COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:ShadowAssistant>/ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/ui $<TARGET_FILE_DIR:ShadowAssistant>/ui
        COMMENT "Copie de l'interface")
add_dependencies(ShadowAssistant shadow_assets)

# Deux paquets, deux dispositions (choisies à la configuration) :
#   défaut                  → archive portable : `cpack -G TGZ` → ShadowAssistant-<version>-linux-x86_64.tar.gz
#   -DSHADOW_DEB_LAYOUT=ON  → paquet .deb : `cpack -G DEB` → shadow-assistant_<version>_amd64.deb
#                             /opt/shadow-assistant + /usr/bin/shadow-assistant + entrée de menu + icône
option(SHADOW_DEB_LAYOUT "Disposition du paquet .deb (/opt, /usr/bin, menu) au lieu de l'archive portable" OFF)

if (SHADOW_DEB_LAYOUT)
    set(APP_DIR opt/shadow-assistant)
else ()
    set(APP_DIR .)
endif ()
install(TARGETS ShadowAssistant RUNTIME DESTINATION ${APP_DIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/ui DESTINATION ${APP_DIR})

if (SHADOW_DEB_LAYOUT)
    install(FILES ${CMAKE_SOURCE_DIR}/installer/linux/shadow-assistant.desktop DESTINATION usr/share/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/installer/linux/shadow-assistant.svg
            DESTINATION usr/share/icons/hicolor/scalable/apps)
    # La commande `shadow-assistant` : le lien est suivi par /proc/self/exe, donc ui/ est trouvé dans /opt.
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/usr/bin\")
        file(CREATE_LINK /opt/shadow-assistant/ShadowAssistant \"\$ENV{DESTDIR}/usr/bin/shadow-assistant\" SYMBOLIC)
    ")

    # Version Debian : « v0.1.0 » → « 0.1.0 » ; un nom de branche ou « dev » devient « 0.0.0~dev ».
    string(REGEX REPLACE "^v" "" _deb_version "${SHADOW_VERSION}")
    if (NOT _deb_version MATCHES "^[0-9]")
        set(_deb_version "0.0.0~${_deb_version}")
    endif ()

    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGING_INSTALL_PREFIX /)
    set(CPACK_PACKAGE_NAME shadow-assistant)
    set(CPACK_PACKAGE_VERSION "${_deb_version}")
    set(CPACK_PACKAGE_CONTACT "Shadow Assistant")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dock en verre : rappels, to-do, minuteur, notes, musique et Claude")
    set(CPACK_DEBIAN_PACKAGE_SECTION utils)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "xdg-utils, wireplumber | pulseaudio-utils")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # Écrite explicitement : sans dpkg (Arch, Fedora…), CPack se rabattrait sur i386.
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
    endif ()
    # Dépendances : calculées d'après les bibliothèques liées quand dpkg-shlibdeps existe (Debian, Ubuntu) ;
    # sinon liste écrite à la main (noms Debian/Ubuntu, avec la variante « t64 » d'Ubuntu 24.04).
    find_program(DPKG_SHLIBDEPS dpkg-shlibdeps)
    if (DPKG_SHLIBDEPS)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    else ()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-3-0 | libgtk-3-0t64, libwebkit2gtk-4.1-0, libcurl4 | libcurl4t64, libx11-6")
    endif ()
else ()
    set(CPACK_PACKAGE_NAME ShadowAssistant)
    set(CPACK_PACKAGE_FILE_NAME "ShadowAssistant-${SHADOW_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_GENERATOR TGZ)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
endif ()
include(CPack)
