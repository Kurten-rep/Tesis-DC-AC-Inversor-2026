function coma(ax)
% Cambia el separador decimal de los ejes a coma.
if nargin < 1, ax = gca; end
for k = 1:numel(ax)
    a = ax(k);
    a.XTickLabel = strrep(cellstr(num2str(a.XTick(:))), '.', ',');
    a.YTickLabel = strrep(cellstr(num2str(a.YTick(:))), '.', ',');
end
end
