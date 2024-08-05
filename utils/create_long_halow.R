
fft.shift <- function(sym) {

	sym <- matrix(sym, ncol=32)

	# the explicit dimensions are required
	# if sym contains only one row
	m1 <- matrix(sym[,17:32], ncol=16)
	m2 <- matrix(sym[, 1:16], ncol=16)

	return(cbind(m1, m2))
}

### symbols as defined in the standard p.3245 eq23-40 for 1 MHz
sym <- c(1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1, 1, 0, -1, -1, -1, 1, -1, -1, -1, 1, -1, 1, 1, 1, -1)
freq <- c(rep(0, 3), sym, rep(0, 2)) # fill empty subcarriers with 0

pre <- fft(fft.shift(freq), inverse=T) / sqrt(26) # 26 is the number of occupied subcarriers, data plus pilot
pre <- Conj(pre)
pre <- rev(pre)


for(i in seq(1, 32, 4)) {
	cat("\t\tgr_complex(", sprintf("% .4f", Re(pre[  i])), ", ", sprintf("% .4f",  Im(pre[  i])), "), ", sep="")
	cat(    "gr_complex(", sprintf("% .4f", Re(pre[i+1])), ", ", sprintf("% .4f",  Im(pre[i+1])), "), ", sep="")
	cat(    "gr_complex(", sprintf("% .4f", Re(pre[i+2])), ", ", sprintf("% .4f",  Im(pre[i+2])), "), ", sep="")
	cat(    "gr_complex(", sprintf("% .4f", Re(pre[i+3])), ", ", sprintf("% .4f",  Im(pre[i+3])), "),\n", sep="")
}
