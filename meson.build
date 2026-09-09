project('myfirstmeson', 'cpp',
        version : '1.0.0',
        default_options : ['warning_level=3', 'cpp_std=c++20'])

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
        dependencies: deps
)

test('test', myfirstmeson)