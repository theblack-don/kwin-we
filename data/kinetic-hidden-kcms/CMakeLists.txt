# Installs the override .desktop files that hide upstream KCMs from the
# Plasma System Settings "Colors & Themes" section. The files are NOT placed
# in ${KDE_INSTALL_APPDIR} (applications/) directly — they live under
# ${KDE_INSTALL_DATADIR}/kineticwe/hidden-kcms/ and the session startup
# script copies them into the user's $XDG_DATA_HOME/applications/ on first
# run, where they take precedence over the upstream .desktop files.
file(GLOB _kinetic_hidden_kcms CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/overrides/*.desktop)
install(FILES ${_kinetic_hidden_kcms}
        DESTINATION ${KDE_INSTALL_DATADIR}/kineticwe/hidden-kcms
)
