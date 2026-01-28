
add_subdirectory(simple-2d-constraint-solver)

set_property(TARGET simple-2d-constraint-solver PROPERTY FOLDER "scs")
set_property(TARGET simple-2d-constraint-solver-test PROPERTY FOLDER "scs")

if (DTV)
    add_subdirectory(direct-to-video)

    set_property(TARGET direct-to-video PROPERTY FOLDER "dtv")
    set_property(TARGET direct-to-video PROPERTY FOLDER "dtv")
endif (DTV)

if (PIRANHA_ENABLED)
    add_subdirectory(piranha)

    set_property(TARGET piranha PROPERTY FOLDER "piranha")
endif (PIRANHA_ENABLED)
