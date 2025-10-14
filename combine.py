import os

def merge_files(file_paths, output_path):
    with open(output_path, 'w') as outfile:
        for file_path in file_paths:
            if os.path.exists(file_path):
                # 提取文件名
                file_name = os.path.basename(file_path)
                # 写入文件名注释
                outfile.write(f"#{file_name}\n\n")
                # 写入文件内容
                with open(file_path, 'r') as infile:
                    outfile.write(infile.read())
                # 可选：在不同文件内容之间添加分隔符
                outfile.write("\n" + "="*80 + "\n")
            else:
                print(f"文件 {file_path} 不存在，跳过。")

if __name__ == "__main__":
    # 定义要合并的文件路径
    files_to_merge = [
        "src/kernel/sched.c",
        "src/kernel/proc.h",
        "src/kernel/proc.c",
        "src/kernel/cpu.h",
        "src/common/rbtree.c"
    ]
    
    # 定义输出文件路径
    output_file = "merged_output.txt"
    
    # 调用函数合并文件
    merge_files(files_to_merge, output_file)
    
    print(f"文件合并完成，输出到 {output_file}")