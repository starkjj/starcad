project('myfirstmeson', 'cpp',
        version : '1.0.0',
        default_options : ['warning_level=3', 'cpp_std=c++20', 'default_library=static']
)

is_web = host_machine.system() == 'emscripten'

# CAD scenes easily exceed 64k vertices per draw list, so use 32-bit draw indices everywhere
# (global arguments also apply to the imgui subproject).
add_global_arguments('-DImDrawIdx=unsigned int', language : 'cpp')

link_args = []
if is_web
        # Browser build: GLFW comes from Emscripten's contrib port and OpenGL is WebGL 2.
        add_global_arguments('-DIMGUI_IMPL_OPENGL_ES3', language : 'cpp')
        web_glfw = ['--use-port=contrib.glfw3']
        meson.override_dependency('glfw3', declare_dependency(compile_args : web_glfw, link_args : web_glfw, version : '3.4.0'))
        meson.override_dependency('gl', declare_dependency())
        link_args += [
                '-sALLOW_MEMORY_GROWTH=1',
                '-sMIN_WEBGL_VERSION=2',
                '-sMAX_WEBGL_VERSION=2',
                '-sDISABLE_EXCEPTION_CATCHING=1',
                '--shell-file', meson.current_source_dir() / 'web' / 'shell.html',
        ]
endif

imgui_backends = [
        'glfw=enabled', 'opengl=enabled',
        'dx9=disabled', 'dx10=disabled', 'dx11=disabled', 'dx12=disabled', 'metal=disabled',
        'vulkan=disabled', 'webgpu=disabled', 'win=disabled', 'osx=disabled', 'allegro5=disabled',
        'sdl2=disabled', 'sdl3=disabled', 'sdl2_renderer=disabled', 'sdl3_renderer=disabled', 'sdl3_gpu=disabled',
]

deps = [
        dependency('glfw3'),
        dependency('imgui', default_options : imgui_backends),
]

if host_machine.system() == 'darwin'
        deps += [
                dependency('Cocoa'),
                dependency('OpenGL'),
                dependency('IOKit'),
                dependency('CoreVideo')
        ]
endif

# Bundle the MinGW C++ runtime so the exe starts outside CLion.
if host_machine.system() == 'windows' and meson.get_compiler('cpp').get_id() == 'gcc'
        link_args += ['-static-libgcc', '-static-libstdc++']
endif

sources = [
        'main.cpp',
        'app.cpp',
        'cad/cad.cpp',
        'cad/commands.cpp',
        'cad/entity.cpp',
        'cad/render.cpp',
        'cad/ui.cpp',
]

myfirstmeson = executable(
        'myfirstmeson',
        sources,
        install : true,
        dependencies : deps,
        link_args : link_args,
        name_suffix : is_web ? 'html' : [],
)

if not is_web
        test('test', myfirstmeson)
endif
