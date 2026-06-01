# Hexagon layers, added inward-out. Each is its own module library so the
# dependency rule (arrows point inward) is enforced by the linker: domain links
# nothing, application links domain, adapter links both, app links all.
# See docs/architecture-v2.md §5.6.
add_subdirectory(domain)
add_subdirectory(application)
add_subdirectory(adapter)
add_subdirectory(app)

# Front-ends (driving adapters). The Qt UI is hoisted to src/ui/ for practical
# reasons (its own deps, resources, MOC) but is architecturally a driving
# adapter. Enabled opt-in once Qt is wired up.
if(AISIM_BUILD_QT_UI)
    add_subdirectory(ui/qt)
endif()
