作者：孙磊
用途：为粘结喷射打造一款使用起来清晰、稳定、便捷的工控机替代软件
日期：2026 / 11 / 11  10：05


解释当前工程
CliFillWorker::run 启动cli 工作线程进切片及数据处理

cli_fill2_convert cli 文件处理成多层.bin文件

process_layer 收集边-填充-保存

fill_edges 扫描线多边形填充核心算法

img_fill_span 填充一段连续像素

postProcessLayerBin 对单层.bin做星光喷头后处理
包括：排偏移 和 旋转

applyRowOffsetBin 按照喷头特性进行排偏移和填充

rotate90ccwBin 进行图像旋转
