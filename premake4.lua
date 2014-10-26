local premakefile_dir = path.translate (os.getcwd ())
local root_dir = premakefile_dir

function fold_left (f, zero, xs)
    local acc = zero
    for _, v in ipairs (xs) do
        acc = f (acc, v)
    end
    return acc
end

local vs_standard_preproc_defs =
    { "NOMINMAX"
    , "WIN32_LEAN_AND_MEAN"
    , "_CRT_SECURE_NO_WARNINGS"
    , "_SCL_SECURE_NO_WARNINGS"
    , "_UNICODE"
    , "UNICODE"
    , "NDEBUG"
    }

local standard_flags =
    { "ExtraWarnings"
    , "NoEditAndContinue"
    , "NoExceptions"
    , "NoManifest"
    , "NoMinimalRebuild"
    , "NoPCH"
    , "NoRTTI"
    , "Symbols"
    , "Unicode"
    }

local base_dir = "src"

function emit_src (dirs)
    location (path.join (base_dir, dirs))
end

function emit_ext (dirs)
    location (fold_left (path.join, base_dir, { "_extlib", dirs }))
end

function final_path (suffixes)
    return fold_left (path.join, path.join ("out", "final"), suffixes)
end

solution "task-homie"
    emit_src ""
    configurations { "Debug", "Release" }
    platforms { "x32", "x64" }
    language "C++"
    targetdir (final_path { })
    objdir (path.join ("out",  "intermediate"))
    buildoptions
        { "/MP"
        , "/d2Zi+"
        , "/sdl-"
        , "/GS-"
        }
    linkoptions
        { "/NODEFAULTLIB"
        , "/pdbaltpath:%_PDB%"
        }
    configuration "Release"
        flags { "OptimizeSize" }
    configuration { "x32" }
        linkoptions
            { "/SUBSYSTEM:WINDOWS,5.01"
            , "/OSVERSION:5.1"
            }
    configuration { "x64" }
        linkoptions
            { "/SUBSYSTEM:WINDOWS,5.02"
            , "/OSVERSION:5.2"
            }

project "task-homie"
    emit_src "task-homie"
    kind "WindowedApp"
    files
        { "src/task-homie/**.h"
        , "src/task-homie/**.cpp"
        , "src/task-homie/**.rc"
        , "src/task-homie-hook/**.hpp"
        }
    defines (vs_standard_preproc_defs)
    flags (standard_flags)
    links
        { "kernel32"
        , "user32"
        , "shell32"
        , "shlwapi"
        }
linkoptions
    { "/Entry:\"entry_point\""
    }

project "task-homie-hook"
    emit_src "task-homie-hook"
    kind "SharedLib"
    files
        { "src/task-homie-hook/**.hpp"
        , "src/task-homie-hook/**.cpp"
        }
    defines (vs_standard_preproc_defs)
    flags (standard_flags)
    links
        { "kernel32"
        , "user32"
        , "gdi32"
        , "shell32"
        }
linkoptions
    { "/ENTRY:\"DllMain\""
    }

function cartesian (a1, a2)
    local ret = {}
    for _, x1 in ipairs (a1) do
        for _, x2 in ipairs (a2) do
            ret[#ret + 1] = { x1, x2 }
        end
    end
    return ret
end

local configs = cartesian (configurations (), platforms ())

for _, val in ipairs (configs) do
    local cfg, platform = unpack(val)
    project "task-homie"
    configuration { cfg, platform }
    targetdir (final_path { platform, cfg, "task-homie" })
end

for _, val in ipairs (configs) do
    local cfg, platform = unpack(val)
    project "task-homie-hook"
    configuration { cfg, platform }
    targetdir (final_path { platform, cfg, "task-homie" })
end
