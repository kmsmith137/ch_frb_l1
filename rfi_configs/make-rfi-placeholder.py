#!/usr/bin/env python

import rf_pipelines as rfp

p1 = rfp.polynomial_detrender(nt_chunk=1024, axis='freq', polydeg=2)
p2 = rfp.polynomial_detrender(nt_chunk=1024, axis='time', polydeg=2)
p3 = rfp.mask_counter(nt_chunk=1024, where="after_rfi")
p = rfp.pipeline([p1,p2,p3])
q = rfp.wi_sub_pipeline(p, nfreq_out=1024, nds_out=1)

rfp.utils.json_write('rfi_placeholder.json', q, clobber=True)
