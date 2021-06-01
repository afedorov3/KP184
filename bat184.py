#!/usr/bin/python

import os, sys, csv, datetime, statistics, argparse
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import matplotlib.ticker as plticker

fnames = ['sample','time','voltage','unitv','current','unitc']
vaunits = {'VA':1, 'mVA':0.001, 'uVA':0.000001}
vunits = {'V':1, 'mV':0.001, 'uV':0.000001}
cunits = {'A':1, 'mA':0.001, 'uA':0.000001}

def mjrFormatter(x, pos):
    return "${:02d}:{:02d}$".format(int(x),int(x % 1 * 60))

def processfile(infile):

    if (len(infile) == 0):
        return
    stopv = 0.5
    ptime = 0.0
    stime = 0.0
    toff = 0.0
    samples = 0
    capacity = 0.0
    energy = 0.0
    t = []
    v = []
    c = []
    with open(infile, newline='') as csvfile:
        dialect = csv.Sniffer().sniff(csvfile.read(1024), delimiters=";,")
        csvfile.seek(0)
        print(infile + ':')
        content = csv.DictReader(csvfile,fieldnames=fnames,dialect=dialect)
        for row in content:
            try: time = float(row['time']) + toff
            except: continue
            voltage = float(row['voltage']) * float(vunits[row['unitv']])
            current = float(row['current']) * float(cunits[row['unitc']])
            if time <= ptime:
                toff = ptime - time
                time = ptime
            if current < 0.002: # noise floor
                if samples == 0: continue
                if (not args.bwhole): break
            if not args.bwhole and (voltage <= stopv) and (samples > 0): break
            if (samples > 0):
                capacity += (current + pcurrent) / 2 * (time - ptime) / 3600
                energy += (current + pcurrent) * (voltage + pvoltage) / 4 * (time - ptime) / 3600
            else: stime = time
            t.append(time / 3600)
            v.append(voltage)
            c.append(current)
            samples += 1
            ptime = time
            pvoltage = voltage
            pcurrent = current

        csvfile.close()

    interval = (time - stime) / samples
    print(' samples: {:d}, mean interval: {:g}s'.format(samples, interval))
    tdelta = datetime.timedelta(seconds = round(time - stime))
    strtime = 'Time: ' + str(tdelta)
    cprefix = ''
    if capacity < 1.0:
        capacity *= 1000.0
        cprefix = 'm'
    strcap = 'Capacity: {:0.2f} {:s}Ah'.format(capacity, cprefix)
    eprefix = ''
    if energy < 1.0:
        energy *= 1000.0
        eprefix = 'm'
    strenr = 'Energy: {:0.2f} {:s}Wh'.format(energy, eprefix)

    print('     ' + strtime)
    print(' '     + strcap)
    print('   '   + strenr)

    if not args.bplot: return

    flabel = {'family' : 'DejaVu Sans', 'weight' : 'bold', 'size': 16, 'color':'0.8'}
    fdescr = {'family' : 'DejaVu Sans', 'weight' : 'bold', 'size': 24, 'color':'0.8'}
    fdata = {'family' : 'DejaVu Sans', 'weight' : 'bold', 'size': 16, 'color':'0.8'}
    ctick = '0.85'
    ctickl = '0.85'
    cgrid = '0.5'
    cface = '0.3'
    cplotv = '#00A2E8'
    cplotc = '#E6A702'

    my_dpi=96
    plt.figure(figsize=(1920/my_dpi, 1080/my_dpi), dpi=my_dpi)
    plt.xlim(-30/3600, t[-1]+30/3600)
    plt.xlabel("Time (h:m)", fontdict=flabel, labelpad=10)
    ax = plt.gca()
    ax.set_facecolor(cface)
    #ax.set_title('Battery discharge curve (' + str(tdelta)+')', fontdict=fdescr, pad=20)
    if t[-1] > 1000:
        loc = plticker.MultipleLocator(base=int(x[-1]/1000)*10)
    elif t[-1] > 100:
        loc = plticker.MultipleLocator(base=int(x[-1]/100)*10)
    elif t[-1] > 10:
        loc = plticker.MultipleLocator(base=1.0)
    elif t[-1] > 1:
        loc = plticker.MultipleLocator(base=0.5)
    else:
        loc = plticker.MultipleLocator(base=5/60)
    ax.xaxis.set_major_locator(loc)
    ax.xaxis.set_major_formatter(mpl.ticker.FuncFormatter(mjrFormatter))
    plt.gcf().autofmt_xdate() # beautify the x-labels
    ax.tick_params(width=2, grid_linewidth=2, length=5, color=ctick, grid_color=ctick, labelcolor=ctickl, labelsize=16)
    ax.grid(linewidth=1, color=cgrid)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.spines['bottom'].set_color(ctick)
    ax.spines['left'].set_color(ctick)

    vmin = min(v) - 0.1
    vmax = max(v) + 0.1
    plt.text(max(t)*0.5, vmax+(vmax - vmin)*0.03, strtime + '  ' + strcap + '  ' + strenr,
        ha="center", va="bottom", fontdict=fdata,
        bbox=dict(boxstyle="round",
                    ec=cface,
                    fc=cface
                    )
        )

    # V
    mcur = statistics.mean(c)
    vlabel = 'Voltage'
    if not args.bplotc:
        vlabel += ' @avg. current '
        if mcur >= 1.0:
            vlabel += '{:.2f} A'.format(mcur)
        else:
            vlabel += '{:.0f} mA'.format(mcur*1000)
    plots = plt.plot(t, v, color=cplotv, linewidth=2, label=vlabel)
    plt.ylim(vmin, vmax)
    plt.ylabel("Voltage (V)", fontdict=flabel, color=cplotv, labelpad=10)
    plt.tick_params('y', labelcolor=cplotv)

    plt.grid(True)

    # C
    if args.bplotc:
        axc = plt.twinx()
        plots += axc.plot(t, c, color=cplotc, linewidth=2, label='Current')
        axc.set_ylim(min(c) - 0.1, max(c) + 0.1)
        axc.set_ylabel("Current (A)", fontdict=flabel, color=cplotc, labelpad=10)
        axc.tick_params('y', labelcolor=cplotc)
        axc.tick_params(width=2, grid_linewidth=2, length=5, color=ctick, grid_color=ctick, labelcolor=cplotc, labelsize=12)
        axc.spines['right'].set_color(ctick)
        axc.spines['bottom'].set_color(ctick)
        axc.spines['left'].set_color(ctick)
        axc.spines['top'].set_visible(False)

    labels = [l.get_label() for l in plots]
    legend = ax.legend(plots, labels, fontsize=16, facecolor=cface, edgecolor=cgrid, framealpha=1.0)
    plt.setp(legend.get_texts(), color=ctickl)

    plotfile = os.path.splitext(infile)[0] + '.png'
    plt.savefig(plotfile, dpi=my_dpi, bbox_inches='tight', facecolor=cface)
    print(' {:s} saved'.format(plotfile))

def main(argv):
    parser = argparse.ArgumentParser(description='121GW battery capacity calculator.')
    parser.add_argument('files', metavar='file.csv', type=str, nargs='+',
                        help='121GW log file path to process')
    parser.add_argument('--plot', '-p', dest='bplot', action='store_const',
                        const=True, default=False,
                        help='Plot to png file')
    parser.add_argument('--plotc', '-c', dest='bplotc', action='store_const',
                        const=True, default=False,
                        help='Also plot current')
    parser.add_argument('--whole', '-w', dest='bwhole', action='store_const',
                        const=True, default=False,
                        help='Read whole file regardless of V/I data')
    global args
    args = parser.parse_args()

    for f in args.files:
        try:
            processfile(f)
        except Exception as e:
            print(str(e))
            pass

if __name__ == "__main__":
    main(sys.argv[1:])
