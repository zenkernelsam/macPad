# D043 frontend review

Independent exact ABI/offset review caught two live and one latent wrong offset constants before deployment. Candidate rejected until fixed and independently tested. Backend D044 already acknowledges and corrects them; no busy injection. Frontend D045 independent arithmetic/segment/reference and reliability audit prepared. Preflight0/1/2 API encoding is not explicit runtime policy-field evidence: EMPTY reply/default remains distinguishable only with raw path evidence. No global input or device acceptance.
