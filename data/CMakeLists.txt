configure_file(org.freedesktop.impl.portal.desktop.lxqt.service.in org.freedesktop.impl.portal.desktop.lxqt.service @ONLY)

install(FILES lxqt-portals.conf DESTINATION "${CMAKE_INSTALL_FULL_DATAROOTDIR}/xdg-desktop-portal")
install(FILES lxqt.portal DESTINATION "${CMAKE_INSTALL_FULL_DATAROOTDIR}/xdg-desktop-portal/portals")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.lxqt.service DESTINATION "${CMAKE_INSTALL_FULL_DATAROOTDIR}/dbus-1/services")
