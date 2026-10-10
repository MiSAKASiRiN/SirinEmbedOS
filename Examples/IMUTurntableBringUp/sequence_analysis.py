"""Revision 2 per-axis trajectory/gyro comparison; never controls hardware."""
import numpy as np

PLANS = {
    1: (5, [(6,7,'forward'), (8,11,'return')]),
    2: (11, [(12,13,'forward'), (14,15,'return')]),
    3: (15, [(16,17,'to_lower'), (18,19,'lower_to_upper'), (20,9,'return')]),
}

def analyze(sources, imus, ranges, directory):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    result = {'revision': 2, 'complete': '9' in ranges and '10' not in ranges,
              'scope': 'Bias-corrected gyro projection; axis inferred from first motion, not independent calibration',
              'axes': {}}
    for motor, (baseline_phase, segments) in PLANS.items():
        if str(baseline_phase) not in ranges:
            continue
        bstart,bend=ranges[str(baseline_phase)]
        encoder=sources[f'turntable.motor{motor}.unwrapped_deg']
        enc=encoder[encoder[:,2]!=0]
        if len(enc)<2:
            continue
        base_enc=enc[(enc[:,0]>bstart+1)&(enc[:,0]<bend-.1)]
        entry={'baseline_seconds':[bstart,bend], 'segments':{},
               'baseline_encoder_span_deg':float(np.ptp(base_enc[:,1])) if len(base_enc) else None}
        sensors={}
        for stream,imu in imus.items():
            name={256:'BMI088',257:'LSM6DS3'}[stream]
            baseline=imu[(imu[:,0]>bstart+1)&(imu[:,0]<bend-.1)]
            if len(baseline)<1000:
                continue
            sensors[name]={'imu':imu,'bias':baseline[:,1:4].mean(axis=0),'axis':None}
        if not sensors:
            entry['unavailable']='Insufficient stationary baseline';result['axes'][str(motor)]=entry
            continue
        for phase,after,label in segments:
            if str(phase) not in ranges:
                continue
            start=ranges[str(phase)][0]
            complete=str(after) in ranges
            end=ranges[str(after)][0] if complete else min(ranges[str(phase)][1],enc[-1,0])-.1
            if end-start<.1:
                continue
            delta=float(np.interp(end,enc[:,0],enc[:,1])-np.interp(start,enc[:,0],enc[:,1]))
            segment={'completed':complete,'start_s':start,'end_s':end,'encoder_delta_deg':delta,'sensors':{}}
            if motor<3 and label=='forward':segment['at_least_one_turn']=complete and delta>=360
            fig,axs=plt.subplots(2,1,figsize=(10,7),sharex=True,constrained_layout=True)
            for name,state in sensors.items():
                imu=state['imu'];rows=imu[(imu[:,0]>=start)&(imu[:,0]<=end)];t=rows[:,0]
                if len(t)<2:continue
                gyro=rows[:,1:4]-state['bias']
                integral=np.vstack((np.zeros(3),np.cumsum((gyro[1:]+gyro[:-1])*.5*np.diff(t)[:,None],axis=0)))*180/np.pi
                if state['axis'] is None:
                    norm=np.linalg.norm(integral[-1])
                    if norm<.1 or abs(delta)<.1:continue
                    state['axis']=integral[-1]/norm*np.sign(delta)
                projected=integral@state['axis']
                actual=np.interp(t,enc[:,0],enc[:,1])-np.interp(t[0],enc[:,0],enc[:,1])
                difference=projected-actual
                gaps=int(np.count_nonzero(np.diff(rows[:,-1])!=1))
                segment['sensors'][name]={'gyro_delta_deg':float(projected[-1]),
                    'encoder_delta_deg':float(actual[-1]),'difference_deg':float(difference[-1]),
                    'rmse_deg':float(np.sqrt(np.mean(difference*difference))),
                    'bias_rad_s':state['bias'].tolist(),'axis_in_sensor_frame':state['axis'].tolist(),
                    'samples':len(t),'sequence_discontinuities':gaps,
                    'max_sample_interval_ms':float(np.diff(t).max()*1000)}
                # Keep plotting bounded even for long records; statistics use all samples.
                stride=max(1,len(t)//10000)
                axs[0].plot((t-start)[::stride],projected[::stride],label=name)
                axs[1].plot((t-start)[::stride],difference[::stride],label=name)
            if segment['sensors']:
                plot_enc=enc[(enc[:,0]>=start)&(enc[:,0]<=end)]
                axs[0].plot(plot_enc[:,0]-start,plot_enc[:,1]-np.interp(start,enc[:,0],enc[:,1]),'k--',label='Encoder')
                axs[0].set(title=f'ID{motor} {label} ({"complete" if complete else "PARTIAL"})',ylabel='Displacement (deg)')
                axs[1].set(xlabel='Time (s)',ylabel='Gyro projection minus encoder (deg)')
                for ax in axs:ax.grid(alpha=.3);ax.legend()
                fig.savefig(directory/f'id{motor}-{label}.png',dpi=140)
            plt.close(fig)
            entry['segments'][label]=segment
        expected=[label for _,_,label in segments]
        entry['completed']=all(label in entry['segments'] and entry['segments'][label]['completed'] for label in expected)
        if entry['completed']:
            entry['loop_scope']='Sum of motion segments; excludes intervening stationary dwells'
            entry['encoder_loop_delta_deg']=sum(v['encoder_delta_deg'] for v in entry['segments'].values())
            entry['gyro_loop_delta_deg']={name:sum(v['sensors'][name]['gyro_delta_deg'] for v in entry['segments'].values())
                for name in sensors if all(name in v['sensors'] for v in entry['segments'].values())}
        result['axes'][str(motor)]=entry
    result['complete'] &= all(result['axes'].get(str(i),{}).get('completed',False) for i in (1,2,3))
    result['complete'] &= all(result['axes'].get(str(i),{}).get('segments',{}).get('forward',{}).get('at_least_one_turn',False) for i in (1,2))
    return result
