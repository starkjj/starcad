project('myfirstmeson', 'cpp',
        version : '1.0.0',
        default_options : ['warning_level=3', 'cpp_std=c++20', 'default_library=static']
)

deps = [
        dependency('glfw3'),
        dependency('imgui'),
        dependency('implot')
]

if host_machine.system() == 'darwin'
        deps += [
                dependency('Cocoa'),
                dependency('OpenGL'),
                dependency('IOKit'),
                dependency('CoreVideo')
        ]
endif

myfirstmeson = executable(
        'myfirstmeson',
        [
                'main.cpp',
                'app.cpp'
        ],
        install : true,
        dependencies: deps,
        link_args: host_machine.system() == 'windows' ? ['-static-libgcc', '-static-libstdc++'] : []
)

test('test', myfirstmeson)