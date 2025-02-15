clear all;

N = 32;
GI = 8;
ltf = [0,  0,  0,  1, -1,  1, -1, -1,  1, -1, 1, 1, -1, 1, 1, 1, 0, -1, -1, -1, 1, -1, -1, -1, 1, -1, 1, 1, 1, -1, 0, 0];
sk = [0.5, -1, 1, -1, -1, -0.5]* (1 + i)* sqrt(2/3)*sqrt(26/6);
%note : sk is multiplied by sqrt(26/6) to match the scaling factor required by the standard for the STF (Table 23-7, p. 3218). Indeed, the scaling factor introduced by the pulse shaping window (see FFT block of PHY Hier)
%is 1/sqrt(26). STF should however be scaled with value 1/sqrt(6) iaw standard. Therefore this additional scaling factor is introduced.

k = [-12, -8, -4, 4, 8, 12];


%%STF >> 4 symbols long
fprintf("(");
for rep=1:4
  fprintf("(");
  for j=-16:15
    filter = (j == k);
    if (sum(filter))
      fprintf(" alpha_mcs * (%d%+dj)",real(sk(filter)), imag(sk(filter)));
    else
      fprintf("0.0");
    endif
    fprintf(", ");
  endfor

  fprintf("),");
endfor


%% 1 LTS (shifted by 1 GI) >> 1 symbol long

fprintf("(");
for h=1:N
  comp = ltf(h)*(e^(- i * 2 * pi * GI / N * (h - 1)));
  fprintf("%d%+dj",real(comp), imag(comp));
  fprintf(", ");
endfor
fprintf("),");

%% 3 LTS (not shifted) >> 3 symbols long

for rep=1:3
  fprintf("(");
  for h=1:N
    fprintf("%d",ltf(h));
    fprintf(", ");
  endfor
  fprintf("),");
endfor
fprintf(")");