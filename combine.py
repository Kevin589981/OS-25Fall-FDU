import os

def merge_files(file_paths, output_path, line_number_style="per_file", path_display_mode="filename"):
    """
    合并多个文件并为每行添加行号
    :param file_paths: 要合并的文件路径列表
    :param output_path: 输出文件路径
    :param line_number_style: 行号样式，"per_file" 每个文件独立计数，"global" 全局连续计数
    :param path_display_mode: 路径显示模式，"filename" 仅显示文件名，"fullpath" 显示完整相对路径
    """
    # 全局行号计数器（如果选择global模式）
    global_line_num = 1
    
    with open(output_path, 'w', encoding='utf-8') as outfile:
        for file_path in file_paths:
            if os.path.exists(file_path):
                # 根据模式选择显示文件名或完整路径
                if path_display_mode == "fullpath":
                    display_text = file_path  # 显示完整相对路径
                else:
                    display_text = os.path.basename(file_path)  # 仅显示文件名
                
                # 写入文件名/路径注释（替换原来的仅写文件名逻辑）
                outfile.write(f"# {display_text}\n\n")
                
                # 单个文件的行号计数器
                file_line_num = 1
                # 读取文件内容并添加行号
                try:
                    with open(file_path, 'r', encoding='utf-8') as infile:
                        for line in infile:
                            # 根据选择的模式确定行号
                            if line_number_style == "global":
                                current_line_num = global_line_num
                                global_line_num += 1
                            else:
                                current_line_num = file_line_num
                                file_line_num += 1
                            
                            # 格式化行号（占6位，右对齐），然后拼接行内容
                            outfile.write(f"{current_line_num:6d}  | {line}")
                            
                            # 处理空行的换行符问题（避免重复换行）
                            if line == "\n":
                                continue
                except Exception as e:
                    print(f"读取文件 {file_path} 时出错：{e}")
                    continue
                
                # 可选：在不同文件内容之间添加分隔符
                outfile.write("\n" + "="*80 + "\n")
            else:
                print(f"文件 {file_path} 不存在，跳过。")

if __name__ == "__main__":
    # 定义要合并的文件路径
    files_to_merge = [
        r"96.0%code/data_collector.py",
        r"96.0%code/train_full_trajectory.py",
        r"96.0%code/eval_full_trajectory.py",
    ]

    # 定义输出文件路径
    output_file = "merged_output.txt"

    # 调用函数：path_display_mode 可选 "filename"（仅文件名）或 "fullpath"（完整路径）
    # 示例1：仅显示文件名（默认）
    # merge_files(files_to_merge, output_file, line_number_style="per_file", path_display_mode="filename")
    # 示例2：显示完整相对路径

if __name__ == "__main__":
    # 定义要合并的文件路径
    files_to_merge = [
        # "src/kernel/sched.c",
        "src/kernel/proc.h",
        "src/kernel/proc.c",
        # "src/kernel/cpu.h",
        # "src/kernel/cpu.c",
        # "src/kernel/pt.c",
        # "src/fs/inode.h",
        # "src/fs/inode.c",
        # "src/fs/cache.h",
        # "src/fs/cache.c",
        # "src/fs/test/inode_test.cpp",
        # "src/driver/virtio_blk.c",
        # "src/common/sem.h",
        # "src/common/buf.h"

        # "src/kernel/syscall.c",
        "src/aarch64/trap.S",
        # "src/test/user_proc_test.c",
        # "src/user/loop.S"
        # "src/fs/file.c",
        # "src/fs/file.h",
        # "src/kernel/proc.h"
    ]
    
    # 定义输出文件路径
    output_file = "merged_output.txt"
    
    merge_files(files_to_merge, output_file, line_number_style="per_file", path_display_mode="fullpath")

    print(f"文件合并完成，输出到 {output_file}")