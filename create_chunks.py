import msgpack

packer = msgpack.Packer(use_bin_type=True)

version = 2
compress = 0
beam_id = 0
fpga_begin = 0
binning = 1
frame0_nano = 0
nrfifreq = 0
has_rfi_mask = False
rfi_mask = 0

nupfreq = 16
nt_per_packet = 16
fpga_counts_per_sample = 384
nt_coarse = 64
nf_coarse = 1024

nt = nt_coarse * nt_per_packet
nf = nf_coarse * nupfreq
fpga_n = fpga_counts_per_sample * nt
nscales = nf_coarse * nt_coarse
ndata = nf * nt
data_size = ndata

scales = np.ones(nscales, np.float32) * (1./256.)
offsets = np.zeros(nscales, np.float32)

# Create some patterned data
#data = np.zeros((nf, nt), np.uint8)
tt,ff = np.meshgrid(np.arange(nt)/nt, np.arange(nf)/nf)
data = 4*(0.25-(ff - 0.5)**2) * (tt**2)
#C = np.random.uniform(size=nt)
#data = (0.25 - (ff - C)**2)

data = (np.clip(255. * (data + 0.1*np.random.normal(size=data.shape)), 1, 254)).astype(np.uint8)

for ich in range(10):

    achunk = ["assembled_chunk in msgpack format",
         version,
         compress,
         data_size,
         beam_id,
         nupfreq, nt_per_packet,fpga_counts_per_sample, nt_coarse,
         nscales, ndata,
         fpga_begin, fpga_n,
         binning,
         bytes(scales.data), bytes(offsets.data),
         bytes(data.data), 
         frame0_nano,
         nrfifreq, has_rfi_mask, rfi_mask,
         ]
    
    bb = packer.pack(achunk)
    fn = 'chunk-%02i.msgpack' % ich
    open(fn, 'wb').write(bb)
    print('Wrote', fn)
    fpga_begin += fpga_n
