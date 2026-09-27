"""Host-only logic tests. Set HOST_CC when gcc is not on PATH."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parent.parent  # tools/ -> demo root
cc=os.environ.get('HOST_CC','gcc')
cases=[
    ('control',['control/src/'+x+'.c' for x in ['control_motor','vehicle_control','control_ipc','producer_api','motor_output_drv8833']]+['control/tests/test_control.c']),
    ('control_motor_contract',['control/src/control_motor.c','CPU0/tools/control_motor_contract_test.c']),
    ('road_navigation_contract',['CPU0/src/road_navigation.c','CPU0/tools/road_navigation_contract_test.c']),
    ('frame_stream_overlay',['CPU0/tools/frame_stream_overlay_contract_test.c']),
    ('frame_stream_bmp',['CPU0/tools/frame_stream_bmp_contract_test.c']),
    ('layout_rotation',['CPU0/src/frame_rotation.c','control/tests/test_layout_rotation.c']),
    ('web_adapter',['control/src/control_ipc.c','control/src/producer_api.c','CPU0/src/ipc_gateway.c','CPU0/src/web_control_adapter.c','control/tests/test_web_adapter.c']),
    ('obstacle_kernels',['CPU0/src/obstacle_kernels.c','control/tests/test_obstacle_kernels.c']),
    ('web_protocol',['M85Web/Application/protocol/control_protocol.c','M85Web/Application/protocol/control_json.c','control/tests/test_web_protocol.c']),
]
with tempfile.TemporaryDirectory(prefix='vehicleoutput-host-test-') as temp_dir:
    out=Path(temp_dir)
    os.environ['TEMP']=os.environ['TMP']=str(out)
    for name,sources in cases:
        exe=out/(name+'.exe')
        defines=['-DIPC_GATEWAY_HOST_TEST'] if name == 'web_adapter' else []
        subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',*defines,'-Icontrol/include','-ICPU0/src','-ICPU0/tools/host_stubs','-IM85Web/Application/protocol',*sources,'-o',str(exe),*(['-lm'] if os.name!='nt' else [])],cwd=root,check=True)
        subprocess.run([str(exe)],cwd=root,check=True)
