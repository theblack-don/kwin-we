add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwin_tiling\")

set(kcm_kwin_tiling_PART_SRCS
    tilingkcm.cpp
)

kconfig_add_kcfg_files(kcm_kwin_tiling_PART_SRCS tilingsettings.kcfgc GENERATE_MOC)
kcmutils_add_qml_kcm(kcm_kwin_tiling SOURCES ${kcm_kwin_tiling_PART_SRCS})

target_link_libraries(kcm_kwin_tiling PRIVATE
    KF6::I18n
    KF6::KCMUtils
    KF6::KCMUtilsQuick
    Qt::DBus
)

install(FILES tilingsettings.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
